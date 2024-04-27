#include <Arduino.h>
#include <microDS18B20.h>
#include <GyverTM1637.h>
#include <OneButton.h>
#include <LiquidCrystal_I2C.h>

#define DS_PIN 2  // пин для термометров
#define DS_SENSOR_AMOUNT 4  //кол-во датчиков
#define DISP_MODE_PIN 3       //пин для экрана РЕЖИМ
#define DISP_SUB_MODE_PIN 4   //пин для экрана СУБРЕЖИМ
#define DISP_TEMP_CUBE_PIN 5  // пин для экрана температуры в кубе
#define DISP_TEMP_PIPE_PIN 6  //пин для экрана температуры в царге
#define DISP_TIME_PIN 7       // пин для экрана отсчета времени
#define DISP_CLK_BLANK_PIN 8  // пин чтобы небыло точек на экране
#define DISP_CLK_TIME_PIN 9   // пин для двоеточия часов


unsigned long current_time = 0;

// объявляем датчики температуры
MicroDS18B20<DS_PIN, DS_ADDR_MODE> sensor[DS_SENSOR_AMOUNT];
// адреса датчиков
uint8_t temp_sensors_addr[][8] = {
  {0x28, 0xDB, 0x85, 0x45, 0xD4, 0x61, 0x68, 0xE4},  // 3m 1 
  {0x28, 0x92, 0x8E, 0x45, 0xD4, 0x7A, 0x43, 0xBB},
  {0x28, 0x16, 0x5C, 0x80, 0xE3, 0xE1, 0x3C, 0x15},  //6
  {0x28, 0x12, 0x8B, 0x80, 0xE3, 0xE1, 0x3C, 0xC6},  //7
  {0x28, 0xC2, 0x9A, 0x80, 0xE3, 0xE1, 0x3C, 0xB3},  //8
  {0x28, 0x21, 0x9D, 0x80, 0xE3, 0xE1, 0x3C, 0x92},  //9
};

// Класс обработчика температуры
class Temp {
  public:

    int values = _values;

    Temp(String name) {
      _name = name;
    }

    void add_value(int value) {
      _sum -= _values[_index];
      _values[_index] = value;
      _sum += value;
      _avg = _sum / 60;
      if (_index == 59) {
        _index = 0;
      } else {
        _index++;
      }
    }

    int get_avg() {
      return _avg;
    }

    void print_avg() {
      Serial.print("Temp_");
      Serial.print(_name);
      Serial.print(" = ");
      Serial.print(_avg);
      Serial.print("C;  ");
    }

    void print_values() {
      Serial.print("Vals_");
      Serial.print(_name);
      Serial.print(" = {");
      for (int i = 0; i < 60; i++) {
        Serial.print(_values[i]);
        Serial.print("; ");
      }
      Serial.print("}   ");
    }

  private:
    int _values[60] = {};
    int _index = 0;
    unsigned long _sum = 0;
    int _avg = 0;
    String _name = "name";
};

Temp temp_cube("Cube");
Temp temp_pipe("Pipe");

// запрос температуры в указанный период
unsigned long last_time_request_temp = 0;
int request_delay = 100;
void request_temp_all() {
  if ((last_time_request_temp == 0) or (current_time - last_time_request_temp > request_delay)) {
    for (int i = 0; i < DS_SENSOR_AMOUNT; i++) {
      sensor[i].requestTemp();
    }
    last_time_request_temp = current_time;
  }
}

// записываем температуры в обработчик в указанный период
unsigned long last_time_add_temp = 0;
int add_value_delay = 50;
void write_temp_to_storage() {
  if ((last_time_add_temp == 0) or (current_time - last_time_add_temp > add_value_delay)) {
    int temp = sensor[0].getTemp() * 100;
    temp_cube.add_value(temp);
    temp = sensor[1].getTemp() * 100;
    temp_pipe.add_value(temp);
    last_time_add_temp = current_time;
  }
}

// вывод температуры в серийный порт
unsigned long last_time_print_temp = 0;
int print_temp_delay = 100;
void print_temp_all() {
  if ((last_time_print_temp == 0) or (current_time - last_time_print_temp > print_temp_delay)) {
    temp_cube.print_avg();
    temp_pipe.print_avg();
    Serial.println();
    // temp_cube.print_values();
    // Serial.println();
    // temp_pipe.print_values();
    // Serial.println();
    last_time_print_temp = current_time;
  }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//                                                                             MODE SELECTION AND USER INTERFACE
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

#define MAIN_BUTTON_PIN 10
#define SERVICE_BUTTON_PIN 11
#define ANALOG_MODE_PIN A7     // пин для крутилки режима

OneButton main_button(MAIN_BUTTON_PIN);
OneButton service_button(SERVICE_BUTTON_PIN);


//  управление режимами
bool confirm_choice = false;
bool confirm_start = false;
bool confirm_stop = false;

bool start_mode = true;
bool selection_mode = false;
bool operation_mode = false;

bool main_mode_selection = false;
bool snab_mode = false;
bool rekt_mode = false;
bool test_mode = false;

bool sub_mode_selection = false;
bool head_mode = false;
bool body_mode = false;

bool flowrate_mode_selection = false;
bool auto_flowrate_mode = false;
bool manual_flowrate_mode = false;
bool flowrate_mode_selection_is_done = false;

bool selection_mode_request = false;
bool operation_mode_request = false;

int fixed_temp = 0;

// Настройка режима работы в режиме настройки, выбор основного режима, выбор субрежима, выбор режима отбора
void change_mode() {
  if (selection_mode == true) {
    if (main_mode_selection == false) {
      if (sub_mode_selection == false) {
        select_and_confirm_area_to_set();
      } else {  // если выбрана настройка режима субрежима, запускается алгоритм выбора опции для режима
        select_and_confirm_sub_mode();
      }
    } else {  // если выбрана настройка режима основного режима, запускается алгоритм выбора опции для режима
      select_and_confirm_main_mode();
    }
  }
}

// Считываем показания потенциометра, возвращаем от 1 до 3 в зависимости от установки
int get_analog_val_3() {
  int val = analogRead(A7);
  if (val < 340) {
    return 1;
  } else if (val >= 340 && val < 682) {
    return 2;
  } else {
    return 3;
  }
}

// Считываем показания потенциометра, возвращаем от 1 до 2 в зависимости от установки
int get_analog_val_2() {
  int val = analogRead(A7);
  if (val < 511) {
    return 1;
  } else {
    return 2;
  }
}

// Выбираем режим для настройки
void select_and_confirm_area_to_set() {
  int val = get_analog_val_2();
  if (val == 1 && confirm_choice == true) {
    main_mode_selection = true;
    sub_mode_selection = false;
  } else if (val == 2 && confirm_choice == true) {
    main_mode_selection = false;
    sub_mode_selection = true;
  }
  confirm_choice = false;
}

// В режиме настройки основного режима выбираем конкретное значение
void select_and_confirm_main_mode() {
  snab_mode = false;
  rekt_mode = false;
  test_mode = false;
  int val = get_analog_val_3();
  if (val == 1 && confirm_choice == true) {
    snab_mode = true;
    rekt_mode = false;
    test_mode = false;
    main_mode_selection = false;
  } else if (val == 2 && confirm_choice == true) {
    snab_mode = false;
    rekt_mode = true;
    test_mode = false;
    main_mode_selection = false;
  } else if (val == 3 && confirm_choice == true) {
    snab_mode = false;
    rekt_mode = false;
    test_mode = true;
    main_mode_selection = false;
  }
  confirm_choice = false;
}

// В режиме настройки суб режима выбираем конкретное значение
void select_and_confirm_sub_mode() {
  head_mode = false;
  body_mode = false; 
  int val = get_analog_val_2();
  if (val == 1 && confirm_choice == true) {
    head_mode = true;
    body_mode = false;
    sub_mode_selection = false;
  } else if (val == 2 && confirm_choice == true) {
    head_mode = false;
    body_mode = true;
    sub_mode_selection = false;
  }
  confirm_choice = false;
}

void print_global_mode() {
  if ((selection_mode + start_mode + operation_mode) > 1) {
    Serial.println("ERROR: More than 1 global mode selected");
  } else {
    if (selection_mode == true) {
      Serial.print("Selection mode is ON;  ");
    }
    if (start_mode == true) {
      Serial.print("Start mode is ON;  ");
    }
    if (operation_mode == true) {
      Serial.print("Operation_mode is ON;  ");
    }
  }
}

void print_area_to_set() {
  if (main_mode_selection + sub_mode_selection + flowrate_mode_selection > 1) {
    Serial.println("ERROR: More than 1 area selected");
  }
  if (selection_mode == true) {
    if (main_mode_selection + sub_mode_selection + flowrate_mode_selection == 0) {
      Serial.print("Area_under_selection: ");
      int val = get_analog_val_3();
      if (val == 1) {
        Serial.print("Main_mode  ");
      }
      if (val == 2) {
        Serial.print("Sub_mode  ");
      }
      if (val == 3) {
        Serial.print("Flowrate_mode  ");
      }
    } else {
      if (main_mode_selection == true) {
        Serial.print("Main_mode in ON;  ");
      }
      if (sub_mode_selection == true) {
        Serial.print("Sub_mode in ON;  ");
      }
      if (flowrate_mode_selection == true) {
        Serial.print("Flowrate_mode in ON;  ");
      }
    }
  }
}

void print_main_mode() {
  if (snab_mode + rekt_mode + test_mode > 1) {
    Serial.println("ERROR: More than 1 main mode selected ");
  }
  if (main_mode_selection == true) {
    if (snab_mode + rekt_mode + test_mode == 0) {
      Serial.print("Main_mode_under_selection: ");
      int val = get_analog_val_3();
      if (val == 1) {
        Serial.print("Snab_mode");
      }
      if (val == 2) {
        Serial.print("Rekt_mode");
      }
      if (val == 3) {
        Serial.print("Test_mode");
      }
    }
  } else {
    if (snab_mode + rekt_mode + test_mode == 1) {
      if (snab_mode == true) {
        Serial.print("Snab_mode in ON;  ");
      }
      if (rekt_mode == true) {
        Serial.print("Rekt_mode in ON;  ");
      }
      if (test_mode == true) {
        Serial.print("Test_mode in ON;  ");
      }
    }
  }
}

void print_sub_mode() {
  if (head_mode + body_mode > 1) {
    Serial.println("ERROR: More than 1 sub mode selected ");
  }
  if (sub_mode_selection == true) {
    if (head_mode + body_mode == 0) {
      Serial.print("Sub_mode_under_selection: ");
      int val = get_analog_val_2();
      if (val == 1) {
        Serial.print("Head_mode");
      }
      if (val == 2) {
        Serial.print("Body_mode");
      }
    }
  } else {
    if (head_mode + body_mode == 1) {
      if (snab_mode == true) {
        Serial.print("Head_mode in ON;  ");
      }
      if (rekt_mode == true) {
        Serial.print("Body_mode in ON;  ");
      }
    }
  }
}

void print_flowrate_mode() {
  if (auto_flowrate_mode + manual_flowrate_mode > 1) {
    Serial.println("ERROR: More than 1 flowrate mode selected ");
  }
  if (flowrate_mode_selection == true) {
    if (auto_flowrate_mode + manual_flowrate_mode == 0) {
      Serial.print("Flowrate_mode_under_selection: ");
      int val = get_analog_val_2();
      if (val == 1) {
        Serial.print("Auto_flowrate_mode");
      }
      if (val == 2) {
        Serial.print("Manual_flowrate_mode");
      }
    }
  } else {
    if (auto_flowrate_mode + manual_flowrate_mode == 1) {
      if (auto_flowrate_mode == true) {
        Serial.print("Auto_flowrate_mode in ON;  ");
      }
      if (manual_flowrate_mode == true) {
        Serial.print("Manual_flowrate_mode in ON;  ");
      }
    }
  }  
}

void print_mode() {
  print_global_mode();
  print_area_to_set();
  print_main_mode();
  print_sub_mode();
  print_flowrate_mode();
  Serial.println();
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//                                                                             RECT SCRYPTS AND VAVLVE OPERATION
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

#define UPPER_VALVE_PIN A0
#define LOWER_VALVE_PIN A1

#define selection_speed_table_len 35
unsigned int selection_speed_table[selection_speed_table_len][3] = { 
  // {SELECTION SPEED, SELECTION DURATION, DELAY DURATION}
  {2200, 6111, 0}, //0
  {2150, 5972, 0}, // 1
  {2100, 5833, 0}, // 2
  {2050, 5694, 0}, // 3
  {2000, 5556, 0}, // 4
  {1950, 5417, 0}, // 5
  {1900, 5278, 0}, // 6
  {1850, 5139, 0}, // 7
  {1800, 5000, 0}, // 8
  {1750, 4861, 0}, // 9
  {1700, 4722, 0}, // 10
  {1650, 4583, 0}, // 11
  {1600, 4444, 0}, // 12
  {1550, 4306, 0}, // 13
  {1500, 4167, 0}, // 14
  {1450, 4028, 0}, // 15
  {1400, 3889, 0}, // 16
  {1350, 3750, 0}, // 17
  {1300, 3611, 0}, // 18
  {1250, 3472, 0}, // 19
  {1200, 3333, 0}, // 20
  {1150, 3194, 0}, // 21
  {1100, 3056, 0}, // 22
  {1050, 2917, 0}, // 23
  {1000, 2778, 0}, // 24
  {900, 2500, 0},  // 25
  {800, 2222, 0},  // 26
  {700, 1944, 0},  // 27
  {600, 1667, 0},  // 28
  {500, 1389, 0},  // 29
  {400, 1111, 0},  // 30
  {300, 833, 0},   // 31
  {250, 603, 0},   // 32
  {150, 0, 0},   // 33
  {30, 0, 0}   // 34
};

bool under_operation = false;
bool stabilization_mode = false;
bool finished = false;
bool upper_valve_is_open = false;
bool lower_valve_is_open = false;

bool scenario_snabby_head_operation = false;
bool scenario_snabby_body_operation = false;
bool scenario_rekt_head_operation = false;
bool scenario_rekt_body_operation = false;
bool scenario_test_operation = false;

unsigned long stabilization_mode_time_started = 0;
unsigned long stabilization_mode_duration = 180000; // 3 min
unsigned long operation_time_started = 0;
unsigned long snabby_head_mode_duration = 5400000; // 1 hr 30 min
unsigned long rekt_head_mode_duration = 10800000; // 3hr 00 min
unsigned long test_mode_duration = 180000; // 3 min

unsigned long test_mode_time_started = 0;

unsigned int temp_fixed = 0;
int temp_diff_snabb = 10;

int selection_speed_body = 0;
int selection_speed_head = 31;
unsigned int totaly_selected = 0;

void selection_speed_body_decrease() {
  selection_speed_body ++;
  lcd_display_selected_speed();
}

void selection_speed_body_increase() {
  selection_speed_body --;
  lcd_display_selected_speed();
}


int find_optimal_selection_speed(int selection_speed_body) {
  int current_temp = temp_cube.get_avg();
  for (int i = selection_speed_body; i < 31; i ++) {
    if (current_temp < selection_speed_table[i][2]) {
      return i;
    }
  }
}

void open_upper_valve() {
  digitalWrite(UPPER_VALVE_PIN, 1);
  upper_valve_is_open = true;
  Serial.print("Valve opened at: ");
  Serial.println(current_time);
}

void close_upper_valve() {
  digitalWrite(UPPER_VALVE_PIN, 0);
  upper_valve_is_open = false;
  Serial.print("Valve closed at: ");
  Serial.println(current_time);
}

void upper_valve_operation(int speed) {
  if (stabilization_mode == false) {
    if ((current_time % 30000) < selection_speed_table[speed][1]) {
      if (upper_valve_is_open == false) {
        open_upper_valve();
      }
    } else {
      if (upper_valve_is_open == true) {
        close_upper_valve();
      }
    }
  }
}

void open_lower_valve() {
  digitalWrite(LOWER_VALVE_PIN, 1);
  lower_valve_is_open = true;
  Serial.print("Valve opened at: ");
  Serial.println(current_time);
}

void close_lower_valve() {
  digitalWrite(LOWER_VALVE_PIN, 0);
  lower_valve_is_open = false;
  Serial.print("Valve closed at: ");
  Serial.println(current_time);
}

void lower_valve_operation(int speed) {
  if (stabilization_mode == false) {
    if ((current_time % 30000) < selection_speed_table[speed][1]) {
      if (lower_valve_is_open == false) {
        open_lower_valve();
      }
    } else {
      if (lower_valve_is_open == true) {
        close_lower_valve();
      }
    }
  }
}

void turn_on_stabilization_mode() {
  if (stabilization_mode == false) {
    close_upper_valve();
    close_lower_valve();
    Serial.println("stabilization_mode is on");
    under_operation = false;
    stabilization_mode = true;
    stabilization_mode_time_started = current_time;
    lcd_display_stabilization_mode();  
  }
}

void turn_off_stabilization_mode() {
  if (stabilization_mode == true) {
    Serial.println("here 1");
    if (temp_in_pipe_more_than_fixed() == false) {
      Serial.println("here 2");
      if (current_time - stabilization_mode_time_started > stabilization_mode_duration) {
        Serial.println("stabilization_mode is off");
        under_operation = true;
        stabilization_mode = false;
        lcd_display_stabilization_mode();
      }
    }
  }
}

int return_temp_diff_fact() {
  return temp_pipe.get_avg() - temp_fixed;
}

bool temp_in_pipe_more_than_fixed() {
  if (return_temp_diff_fact() > temp_diff_snabb) {
    return true;
  }
  else {
    return false;
  }
}

void serial_display_temp_failure_report() {
  Serial.print("!Temp failure! Temp in pipe = ");
  Serial.print(temp_pipe.get_avg());
  Serial.print("; Fixed temp = ");
  Serial.print(temp_fixed);
  Serial.print("; Diff = ");
  Serial.println(return_temp_diff_fact());
}

void check_temp_and_start_stabilization_mode_if_temp_not_ok() {
  if (temp_in_pipe_more_than_fixed() == true) {
    if (stabilization_mode == false) {
      serial_display_temp_failure_report();
      selection_speed_body_decrease();
      turn_on_stabilization_mode();
    }
  } else {
    turn_off_stabilization_mode();
  }
}

void scenario_snabby_head() {
  if (scenario_snabby_head_operation == false) {
    operation_time_started = current_time;
    selection_speed_head = selection_speed_table_len - 4;
    scenario_snabby_head_operation = true;
    under_operation = true;
    Serial.println("scenario_snabby_head started");
    lcd_set_display_in_operation_update();
  }
  if (current_time - operation_time_started < snabby_head_mode_duration) {
    upper_valve_operation(selection_speed_head);
  } else {
    close_upper_valve();
    finished = true;
    confirm_start = false;
    scenario_snabby_head_operation = false;
    under_operation = false;
  }
}

void scenario_snabby_body() {
  if (scenario_snabby_body_operation == false) {
    temp_fixed = temp_pipe.get_avg();
    scenario_snabby_body_operation = true;
    under_operation = true;
    selection_speed_body = 0;
    Serial.println("scenario_snabby_body started");
    lcd_set_display_in_operation_update();
  }
  // selection_speed_body = find_optimal_selection_speed(selection_speed_body);
  check_temp_and_start_stabilization_mode_if_temp_not_ok();
  if (temp_cube.get_avg() < 9900) {
    upper_valve_operation(selection_speed_body);
  } else {
    close_upper_valve();
    finished = true;
    confirm_start = false;
    scenario_snabby_body_operation = false;
    under_operation = false;
  }
}

void scenario_rekt_head() {
  if (scenario_rekt_head_operation == false) {
    operation_time_started = current_time;
    selection_speed_head = selection_speed_table_len - 2;
    scenario_rekt_head_operation = true;
    under_operation = true;
    Serial.println("scenario_rekt_head started");
    lcd_set_display_in_operation_update();
  }
  if (current_time - operation_time_started < rekt_head_mode_duration) {
    upper_valve_operation(selection_speed_head);
  } else {
    close_upper_valve();
    finished = true;
    confirm_start = false;
    scenario_rekt_head_operation = false;
    under_operation = false;
  }
}

void scenario_rekt_body() {
  if (scenario_rekt_body_operation == false) {
    temp_fixed = temp_pipe.get_avg();
    scenario_rekt_body_operation = true;
    under_operation = true;
    selection_speed_body = 0;
    Serial.println("scenario_rekt_body started");
    selection_speed_head = selection_speed_table_len - 1;
    lcd_set_display_in_operation_update();
  }
  selection_speed_body = find_optimal_selection_speed(selection_speed_body);
  check_temp_and_start_stabilization_mode_if_temp_not_ok();
  if (temp_cube.get_avg() < 9600) {
    upper_valve_operation(selection_speed_head);
    lower_valve_operation(selection_speed_body);
  } else {
    close_lower_valve();
    close_upper_valve();
    finished = true;
    confirm_start = false;
    scenario_snabby_body_operation = false;
    under_operation = false;
  }
}

void scenario_test() {
  if (scenario_test_operation == false) {
    operation_time_started = current_time;
    selection_speed_body = 0;
    scenario_test_operation = true;
    under_operation = true;
    Serial.println("scenario_test started");
    lcd_set_display_in_operation_update();
  } 
  if ((current_time - operation_time_started) < test_mode_duration) {
    upper_valve_operation(selection_speed_body);
  } else {
    close_upper_valve();
    Serial.println("scenario_test finished");
    scenario_test_operation = false;
    finished = true;
    confirm_start = false;
    under_operation = false;
  }
}

void implement_mode () {
  if (operation_mode == true) {
    if (snab_mode == true) {
      if (head_mode == true) {
        scenario_snabby_head();
      } else if (body_mode == true) {
        scenario_snabby_body();
      }
    } else if (rekt_mode == true) {
      if (head_mode == true) {
        scenario_rekt_head();
      } else if (body_mode == true) {
        scenario_rekt_body();
      }
    } else if (test_mode == true) {
      scenario_test();
    }
  }
}

void operation() {
  if (confirm_start == true) {
    implement_mode();
  } 
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//                                                                               DISPLAY SCRYPTS
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


// инициализируем дисплеи
LiquidCrystal_I2C lcd(0x27, 20, 4);

// показать температуру на дисплеях
unsigned long last_time_display_upd = 0;
int general_blink_delay = 2000;
int selection_in_menu_blink_delay = general_blink_delay / 2;
int selection_blink_delay = selection_in_menu_blink_delay / 2;


#define lcd_main_mode_coordinates 11, 0
#define lcd_sub_mode_coordinates 11, 1
#define lcd_selection_speed_coordinates 11, 3
#define lcd_operation_status_coordinates 5, 0
#define lcd_stabilization_status_coordinates 5, 1
#define lcd_temp_pipe_coordinates 5, 2
#define lcd_temp_cube_coordindates 5, 3

void lcd_display_main_mode_in_operation() {
  lcd.setCursor(lcd_main_mode_coordinates);
  lcd.print(lcd_00_00_selected_mode_text());
}

void lcd_display_sub_mode_in_operation() {
  lcd.setCursor(lcd_sub_mode_coordinates);
  lcd.print(lcd_00_01_selected_sub_mode_text());
}

void lcd_display_stabilization_mode() {
  lcd.setCursor(lcd_stabilization_status_coordinates);
  lcd.print(lcd_05_01_Stab_status_text());
}

void lcd_display_temp_pipe() {
  lcd.setCursor(lcd_temp_pipe_coordinates);
  lcd.print(temp_pipe.get_avg());
}

void lcd_display_temp_cube() {
  lcd.setCursor(lcd_temp_cube_coordindates);
  lcd.print(temp_cube.get_avg());
}

void lcd_set_display_in_operation_general() {
  lcd_display_main_mode_in_operation();
  lcd_display_sub_mode_in_operation();
  lcd_display_operation_status();
}

void lcd_set_display_in_selection_general() {
  lcd.setCursor(lcd_main_mode_coordinates);
  lcd.print("    ");
  lcd.setCursor(lcd_sub_mode_coordinates);
  lcd.print("    ");
  lcd.setCursor(lcd_selection_speed_coordinates);
  lcd.print("    ");
  lcd.setCursor(lcd_operation_status_coordinates);
  lcd.print("     ");
  lcd.setCursor(lcd_stabilization_status_coordinates);
  lcd.print("     ");
  lcd.setCursor(lcd_temp_pipe_coordinates);
  lcd.print("     ");
  lcd.setCursor(lcd_temp_cube_coordindates);
  lcd.print("     ");
}

void lcd_set_display_in_operation_update() {
  lcd_display_selected_speed();
  lcd_display_operation_status();
  lcd_display_stabilization_mode();
}

void lcd_display_temperatures() {
  if (operation_mode == true) {
    lcd_display_temp_pipe();
    lcd_display_temp_cube();
  }
}

void lcd_update_data_in_selection() {
  if (selection_mode == true) {
    lcd_display_main_mode_in_selection();
    lcd_display_sub_mode_in_selection();
  }
}

void lcd_display_sub_mode_in_selection() {
  lcd.setCursor(lcd_sub_mode_coordinates);
  lcd.print(lcd_00_01_sub_mode());
}

void lcd_display_main_mode_in_selection() {
  lcd.setCursor(lcd_main_mode_coordinates);
  lcd.print(lcd_display_main_mode_in_selection_text());
}

void lcd_display_data() {
  lcd_update_data_in_selection();
  lcd_display_temperatures();
}



String lcd_operation_status_text() {
  if (selection_mode == true) {
    return "Selct";
  } else if (operation_mode == true) {
    if (under_operation == true) {
      if (confirm_start == true) {
        return "Oprtn";
      } else {
        return "Pause";
      }
    } else if (finished == true) {
      return "Finsh";
    } else {
      return "W84Cf";
    }
  } else {
    return "Start";
  }
}

void lcd_display_operation_status() {
  lcd.setCursor(lcd_operation_status_coordinates);
  lcd.print(lcd_operation_status_text());
}

String lcd_05_01_Stab_status_text() {
  if (under_operation == true) {
    return "Basic";
  } else if (stabilization_mode == true) {
    return "Stabs";
  }
  return "     ";
}

void lcd_display_selection_speed_in_start_and_select() {
  lcd.setCursor(lcd_selection_speed_coordinates);
  lcd.print("    ");
}

void lcd_display_selection_speed_in_opration(int selection_speed) {
  if (selection_speed_table[selection_speed][0] < 1000) {
    lcd.setCursor(lcd_selection_speed_coordinates);
    lcd.print(" ");
    lcd.print(selection_speed_table[selection_speed][0]);
  } else {
    lcd.setCursor(lcd_selection_speed_coordinates);
    lcd.print(selection_speed_table[selection_speed][0]);
  }
}

void lcd_display_selected_speed() {
  if (start_mode == true or selection_mode == true) {
    lcd_display_selection_speed_in_start_and_select();
  } else if (operation_mode == true) {
    if (body_mode == true) {
      lcd_display_selection_speed_in_opration(selection_speed_body);
    } else if (head_mode == true) {
      lcd_display_selection_speed_in_opration(selection_speed_head);
    }
  }
}


String lcd_00_00_selected_mode_text() {
  if (rekt_mode == true) {
    return "Rect";
  } else if (snab_mode == true) {
    return "Snab";
  } else if (test_mode == true) {
    return "Test";
  } else {
    return "    ";
  }
}

String lcd_00_00_selection_mode_main_mode_selection() {
  if ((current_time % selection_blink_delay) < (selection_blink_delay / 2)) {
    return "    ";
  } else {
    switch (get_analog_val_3()) {
      case 1: return "Snab"; break;
      case 2: return "Rect"; break;
      case 3: return "Test"; break;
    }
  }
}

String lcd_00_00_selection_mode_main_menu () {
  if ((get_analog_val_2() == 1) && ((main_mode_selection + sub_mode_selection) == 0)) {
    if ((current_time % selection_in_menu_blink_delay) < (selection_in_menu_blink_delay / 2)) {
      return "----";
    } else {
      return lcd_00_00_selected_mode_text();
    }
  } else {
    return lcd_00_00_selected_mode_text();
  }
}

String lcd_display_main_mode_in_selection_text() {
  if (main_mode_selection == true) {
    return lcd_00_00_selection_mode_main_mode_selection();
  } else {
    return lcd_00_00_selection_mode_main_menu();
  }
}




// void lcd_00_00_Mode_display_data() {
//   lcd.setCursor(0, 0);
//   lcd.print(lcd_00_00_Mode());
// }

String lcd_00_01_selected_sub_mode_text() {
  if (head_mode == true) {
    return "Head";
  } else if (body_mode == true) {
    return "Body";
  } else {
    return "    ";
  }
}

String lcd_00_01_selection_mode_sub_mode_selection() {
  if ((current_time % selection_blink_delay) < (selection_blink_delay / 2)) {
    return "    ";
  } else {
    switch (get_analog_val_2()) {
      case 1: return "Head"; break;
      case 2: return "Body"; break;
    }
  }
}

String lcd_00_01_selection_mode_main_menu () {
  if ((get_analog_val_2() == 2) && ((main_mode_selection + sub_mode_selection) == 0)) {
    if ((current_time % selection_in_menu_blink_delay) < (selection_in_menu_blink_delay / 2)) {
      return "----";
    } else {
      return lcd_00_01_selected_sub_mode_text();
    }
  } else {
    return lcd_00_01_selected_sub_mode_text();
  }
}

String lcd_00_01_sub_mode() {
  if (sub_mode_selection == true) {
    return lcd_00_01_selection_mode_sub_mode_selection();
  } else {
    return lcd_00_01_selection_mode_main_menu();
  }
}

void lcd_00_01_sub_mode_display_data() {
  lcd.setCursor(0, 1);
  lcd.print(lcd_00_01_sub_mode());
}

void lcd_00_02_Temp_set_display_data() {
  if (operation_mode == true) {
    lcd.setCursor(0, 2);
    lcd.print(temp_fixed);  
  } else {
    lcd.setCursor(0, 2);
    lcd.print("    ");
  }
}




void lcd_05_01_Stab_status_display_data() {
  lcd.setCursor(5, 1);
  lcd.print(lcd_05_01_Stab_status_text());
}

void lcd_05_02_Temp_pipe_display_data() {
  lcd.setCursor(5, 2);
  lcd.print(temp_pipe.get_avg());
}

void lcd_05_03_Temp_cube_display_data() {
  lcd.setCursor(5, 3);
  lcd.print(temp_cube.get_avg());
}

void lcd_update_data_in_selection_mode() {
  if (current_time - last_time_display_upd > 100) {
    if (selection_mode == true) {

    } else if (operation_mode == true) {

    }

    // lcd_00_00_Mode_display_data();
    lcd_00_01_sub_mode_display_data();
    lcd_00_02_Temp_set_display_data();
    lcd_05_01_Stab_status_display_data();
    lcd_05_02_Temp_pipe_display_data();
    lcd_05_03_Temp_cube_display_data();
    last_time_display_upd = current_time;
  }
}

// функция для однократного нажатия кнопки: кнопка подтверждает выбор
void single_click() {
  if (selection_mode == true) {
    confirm_choice = true;
  } else if (operation_mode == true) {
    confirm_start = !confirm_start;
    lcd_display_operation_status();
  }
}

void service_single_click() {
  if (selection_speed_body < (selection_speed_table_len)) {
    selection_speed_body_decrease();
  } 
}

void service_double_click() {
  if (selection_speed_body > 0) {
    selection_speed_body_increase();
  }
}

void operation_mode_turn_on() {
  selection_mode = false;
  start_mode = false;
  operation_mode = true;

  under_operation = false;
  stabilization_mode = false;
  finished = false;
  upper_valve_is_open = false;
  lower_valve_is_open = false;

  scenario_snabby_head_operation = false;
  scenario_snabby_body_operation = false;
  scenario_rekt_head_operation = false;
  scenario_rekt_body_operation = false;
  scenario_test_operation = false;
  lcd_set_display_in_operation_general() ;
}

// функция для долгого нажатия кнопки: 1 - переход в режим настройки, 2 - переход в режим работы
void change_global_mode() {
  // если режим настройки не выбран, включаем его
  if (selection_mode == false) {
    selection_mode = true;
    start_mode = false;
    operation_mode = false;

    under_operation = false;
    stabilization_mode = false;
    finished = false;
    upper_valve_is_open = false;
    lower_valve_is_open = false;

    scenario_snabby_head_operation = false;
    scenario_snabby_body_operation = false;
    scenario_rekt_head_operation = false;
    scenario_rekt_body_operation = false;
    scenario_test_operation = false;

    confirm_start = false;
    close_lower_valve();
    close_upper_valve();
    lcd_set_display_in_selection_general();
  }
  // если находимся в режиме настройки, то повторное удержание переведет в режим работы
  else {
    operation_mode_turn_on();   
  }
}

void setup() {

  // pinMode(A6, INPUT);
  pinMode(A7, INPUT);
  pinMode(UPPER_VALVE_PIN, OUTPUT);
  pinMode(LOWER_VALVE_PIN, OUTPUT);

  // открываем порт
  Serial.begin(9600);
  pinMode(MAIN_BUTTON_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();


  // инициализируем датчики температуры
  for (int i = 0; i < DS_SENSOR_AMOUNT; i++) {
    sensor[i].setAddress(temp_sensors_addr[i]);
  }

  main_button.attachLongPressStart(change_global_mode);
  main_button.attachClick(single_click);
  service_button.attachClick(service_single_click);
  service_button.attachDoubleClick(service_double_click);
}

void loop() {
  current_time = millis();
  main_button.tick();
  service_button.tick();
  operation();
  write_temp_to_storage();
  request_temp_all();
  change_mode();
  lcd_display_data();
}