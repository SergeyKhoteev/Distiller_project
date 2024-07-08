#include <Arduino.h>
#include <microDS18B20.h>
#include <GyverTM1637.h>
#include <OneButton.h>
#include <LiquidCrystal_I2C.h>
#include "GyverWDT.h"

#define DS_PIN 2               // пин для термометров
#define MAIN_BUTTON_PIN 10     // пин основной кнопки
#define SERVICE_BUTTON_PIN 11  // пин сервисной кнопки
#define UPPER_VALVE_PIN A0     // пин управления клапаном верхним
#define LOWER_VALVE_PIN A1     // пин управления клапаном нижним
#define ANALOG_MODE_PIN A7     // пин для крутилки режима

// инициализируем LCD дисплей
LiquidCrystal_I2C lcd(0x27, 20, 4);

// инициализируем кнопки
OneButton main_button(MAIN_BUTTON_PIN);
OneButton service_button(SERVICE_BUTTON_PIN);

// инициализируем датчики температуры
#define DS_SENSOR_AMOUNT 4  //кол-во датчиков
MicroDS18B20<DS_PIN, DS_ADDR_MODE> sensor[DS_SENSOR_AMOUNT];


unsigned long current_time = 0;
bool error_mode = false;

// адреса датчиков
uint8_t temp_sensors_addr[][8] = {
  { 0x28, 0xC2, 0x9A, 0x80, 0xE3, 0xE1, 0x3C, 0xB3 },  // 3m 1
  { 0x28, 0xFA, 0xE3, 0x80, 0xE3, 0xE1, 0x3C, 0x61 },
  { 0x28, 0x16, 0x5C, 0x80, 0xE3, 0xE1, 0x3C, 0x15 },  //6
  { 0x28, 0x12, 0x8B, 0x80, 0xE3, 0xE1, 0x3C, 0xC6 },  //7
  { 0x28, 0xC2, 0x9A, 0x80, 0xE3, 0xE1, 0x3C, 0xB3 },  //8
  { 0x28, 0x21, 0x9D, 0x80, 0xE3, 0xE1, 0x3C, 0x92 },  //9
};

// Класс обработчика температуры
class Temp {
  public:
  #define len 10
    int values = _values;

    Temp(String name) {
      _name = name;
    }

    void add_value(int value) {
      _sum -= _values[_index];
      _values[_index] = value;
      _sum += value;
      _avg = _sum / len;
      if (_index == (len - 1)) {
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
      for (int i = 0; i < len; i++) {
        Serial.print(_values[i]);
        Serial.print("; ");
      }
      Serial.print("}   ");
    }

  private:
    int _values[len] = {};
    int _index = 0;
    unsigned long _sum = 0;
    int _avg = 0;
    String _name = "name";
};



class Clock_mode {

  public:

    Clock_mode(unsigned long start, unsigned long dur) {
      _start = start;
      _duration = dur;
    }

    void start_time(unsigned long start_time) {
      _start = start_time;
    }
    
    void duration_time(unsigned long duration_time) {
      _duration = duration_time;
    }

    void now_time() {
      _now = current_time;
    }

    void seconds_passed() {
      _seconds_passed = _now - _start;
      _seconds_passed /= 1000;
    }

    // void seconds_remains() {
    //   _seconds_remains = (_duration / 1000) - _seconds_passed;
    // }

    void hours_passed() {
      _hours_passed = _seconds_passed / 3600;
      _seconds_passed -= _hours_passed * 3600;
    }

    void minutes_passed() {
      _minutes_passed = _seconds_passed / 60;
      _seconds_passed -= _minutes_passed * 60;
    }

    // void hours_remains() {
    //   _hours_remains = _seconds_remains / 3600;
    //   _seconds_remains -= _hours_remains * 3600;
    // }

    // void minutes_remains() {
    //   _minutes_remains = _seconds_remains / 60;
    //   _seconds_remains -= _minutes_remains * 60;
    // }    

    int return_hours_passed() {
      return _hours_passed;
    }

    int return_minutes_passed() {
      return _minutes_passed;
    }

    int return_seconds_passed() {
      return _seconds_passed;
    }

    // int return_hours_remains() {
    //   return _hours_remains;
    // }

    // int return_minutes_remains() {
    //   return _minutes_remains;
    // }

    // int return_seconds_remains() {
    //   return _seconds_remains;
    // }

    void update_values() {
      now_time();
      seconds_passed();
      // seconds_remains();
      hours_passed();
      minutes_passed();
      // hours_remains();
      // minutes_remains();
    }

    void print() {
      Serial.print("_start"); 
      Serial.print(_start);
      Serial.print("_duration"); 
      Serial.print(_duration);
      Serial.print("_now"); 
      Serial.println(_now);
    }

    void update_init(unsigned long start, unsigned long duration) {
      _start = start;
      _duration = duration / 1000;
      update_values();
    }

  private:
    unsigned long _start = 0;
    unsigned long _duration = 0;
    unsigned long _now = 0;
    unsigned long _seconds_passed = 0;
    unsigned long _seconds_remains = 0;
    unsigned int _hours_passed = 0;
    unsigned int _minutes_passed = 0;
    unsigned int _hours_remains = 0;
    unsigned int _minutes_remains = 0;
};

Clock_mode mode_timer(0, 0);

Temp temp_cube("Cube");
Temp temp_pipe("Pipe");

#define time_update_delay 250
unsigned int last_time_update_timer = 0;
void update_timer() {
  if (current_time - last_time_update_timer > time_update_delay) {
    mode_timer.update_values();
    lcd_display_timer();
    last_time_update_timer = current_time;
  }
}

// запрос температуры в указанный период
unsigned long last_time_request_temp = 0;
#define temp_request_delay 750
void request_temp_all() {
  for (int i = 0; i < DS_SENSOR_AMOUNT; i++) {
    sensor[i].requestTemp();
  }
}

void turn_on_error_mode() {
  if (error_mode == false) {
    error_mode = true;
  }
}

void turn_off_error_mode() {
  if (error_mode == true) {
    error_mode = false;
  }
}

// записываем температуры в обработчик в указанный период
void write_temp_to_storage() {
  if (sensor[0].readTemp() == true) {
    turn_off_error_mode();
    int temp = sensor[0].getTemp() * 100;
    temp += 100;
    temp_cube.add_value(temp);
  } else {
    turn_on_error_mode();
  }
  if (sensor[1].readTemp() == true) {
    turn_off_error_mode();
    int temp = sensor[1].getTemp() * 100;
    temp_pipe.add_value(temp);
  } else {
    turn_on_error_mode();
  }
}

void write_temp_and_request_new() {
  if (current_time - last_time_request_temp > temp_request_delay) {
    write_temp_to_storage();
    request_temp_all();
    last_time_request_temp = current_time;
  }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//                                                                             MODE SELECTION AND USER INTERFACE
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------




//  управление режимами
bool confirm_choice = false;
bool confirm_start = false;

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
  if (main_mode_selection + sub_mode_selection > 1) {
    Serial.println("ERROR: More than 1 area selected");
  }
  if (selection_mode == true) {
    if (main_mode_selection + sub_mode_selection == 0) {
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


void print_mode() {
  print_global_mode();
  print_area_to_set();
  print_main_mode();
  print_sub_mode();
  Serial.println();
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//                                                                             RECT SCRYPTS AND VAVLVE OPERATION
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------



#define upper_valve_selection_speed_table_len 35
unsigned int upper_valve_selection_speed_table[upper_valve_selection_speed_table_len][3] = {
  // {SELECTION SPEED, SELECTION DURATION, DELAY DURATION}
  { 2200, 6569, 9125 },  //0
  { 2150, 6419, 9167 },  //1
  { 2100, 6270, 9195 },  //2
  { 2050, 6121, 9215 },  //3
  { 2000, 5972, 9237 },  //4
  { 1950, 5823, 9260 },  //5
  { 1900, 5673, 9280 },  //6
  { 1850, 5524, 9297 },  //7
  { 1800, 5375, 9310 },  //8
  { 1750, 5225, 9320 },  //9
  { 1700, 5076, 9331 },  //10
  { 1650, 4926, 9343 },  //11
  { 1600, 4777, 9359 },  //12
  { 1550, 4628, 9371 },  //13
  { 1500, 4479, 9380 },  //14
  { 1450, 4330, 9392 },  //15
  { 1400, 4180, 9404 },  //16
  { 1350, 4031, 9424 },  //17
  { 1300, 3881, 9444 },  //18
  { 1250, 3732, 9450 },  //19
  { 1200, 3582, 9464 },  //20
  { 1150, 3433, 9475 },  //21
  { 1100, 3285, 9485 },  //22
  { 1050, 3135, 9485 },  //23
  { 1000, 2986, 9519 },  //24
  { 900, 2687, 9540 },   //25
  { 800, 2388, 9580 },   //26
  { 700, 2089, 9605 },   //27
  { 600, 1792, 9660 },   //28
  { 500, 1493, 9900 },   //29
  { 400, 1194, 0 },      //30
  { 300, 860, 0 },       //31
  { 200, 574, 0 },       //32
  { 150, 429, 0 },       //33
  { 30, 86, 0 },         //34
};


#define lower_valve_selection_speed_table_len 36
unsigned int lower_valve_selection_speed_table[lower_valve_selection_speed_table_len][3] = {
  // {SELECTION SPEED, SELECTION DURATION, DELAY DURATION}
  { 2500, 3287, 9000 },  //0 tbd 1
  { 2450, 3221, 9001 },  //1
  { 2400, 3156, 9002 },  //2 true
  { 2350, 3127, 9003 },  //3
  { 2300, 3078, 9004 },  //4
  { 2250, 3030, 9050 },  //5 tbd 1
  { 2200, 2997, 9125 },  //6
  { 2150, 2946, 9167 },  //7
  { 2100, 2895, 9195 },  //8 tbd 1
  { 2050, 2857, 9215 },  //9
  { 2000, 2788, 9237 },  //10
  { 1950, 2750, 9260 },  //11 tbd 1
  { 1900, 2694, 9280 },  //12
  { 1850, 2639, 9297 },  //13
  { 1800, 2582, 9310 },  //14
  { 1750, 2525, 9320 },  //15 true
  { 1700, 2503, 9331 },  //16
  { 1650, 2430, 9343 },  //17
  { 1600, 2404, 9359 },  //18 tbd 1
  { 1550, 2352, 9371 },  //19
  { 1500, 2299, 9380 },  //20
  { 1450, 2244, 9392 },  //21
  { 1400, 2188, 9404 },  //22 tbd 1
  { 1350, 2150, 9424 },  //23
  { 1300, 2070, 9444 },  //24
  { 1250, 2028, 9450 },  //25 tbd 1
  { 1200, 1964, 9464 },  //26
  { 1150, 1900, 9475 },  //27
  { 1100, 1834, 9485 },  //28
  { 1050, 1767, 9485 },  //29 true
  { 1000, 1712, 9519 },  //30
  { 900, 1658, 9540 },   //31
  { 800, 1418, 9580 },   //32
  { 700, 1262, 9605 },   //33 true
  { 600, 1082, 9660 },   //34
  { 500, 901, 9900 },    //35 tbd 1
};
// {2200, 6569, 9125},  //0
// {2150, 6419, 9167},  //1
// {2100, 6270, 9195},  //2
// {2050, 6121, 9215},  //3
// {2000, 5972, 9237},  //4
// {1950, 5823, 9260},  //5
// {1900, 5673, 9280},  //6
// {1850, 5524, 9297},  //7
// {1800, 5375, 9310},  //8
// {1750, 5225, 9320},  //9
// {1700, 5076, 9331},  //10
// {1650, 4926, 9343},  //11
// {1600, 4777, 9359},  //12
// {1550, 4628, 9371},  //13
// {1500, 4479, 9380},  //14
// {1450, 4330, 9392},  //15
// {1400, 4180, 9404},  //16
// {1350, 4031, 9424},  //17
// {1300, 3881, 9444},  //18
// {1250, 3732, 9450},  //19
// {1200, 3582, 9464},  //20
// {1150, 3433, 9475},  //21
// {1100, 3285, 9485},  //22
// {1050, 3135, 9485},  //23
// {1000, 2986, 9519},  //24
// {900, 2687, 9540},  //25
// {800, 2388, 9580},  //26
// {700, 2089, 9605},  //27
// {600, 1792, 9660},  //28
// {500, 1493, 9900},  //29
// {400, 1194, 0},  //30
// {300, 860, 0},  //31
// {250, 717, 0},  //32
// {150, 429, 0},  //33
// {30, 86, 0},  //34temp_diff

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
unsigned long stabilization_mode_duration = 180000;  // 3 min
unsigned long operation_time_started = 0;
unsigned long snabby_head_mode_duration = 3600000;  // 1 hr 00 min
unsigned long snabby_body_mode_duration = 14400000;   // 4hr 00 min
unsigned long rekt_head_mode_duration = 10800000;   // 3hr 00 min
unsigned long rekt_body_mode_duration = 32400000;   // 9hr 00 min
unsigned long test_mode_duration = 540000;          // 9 min

unsigned long test_mode_time_started = 0;

unsigned int temp_fixed = 0;
int temp_diff = 0;
int decrease_lim = 0;

int selection_speed_body = 0;
int selection_speed_head = 31;
unsigned int totaly_selected = 0;

int fails_total = 0;

void update_temp_fixed() {
  temp_fixed = temp_pipe.get_avg();
}

void update_temp_fixed_at_91() {
  if (temp_cube.get_avg() > 9100 and temp_cube.get_avg() < 9110) {
    update_temp_fixed();
  }
  lcd_display_temp_fixed();
}

void selection_speed_body_decrease() {
  if (selection_speed_body < decrease_lim) {
    selection_speed_body++;
    lcd_display_selection_speed_body();
  }
}

void selection_speed_body_increase() {
  selection_speed_body--;
  lcd_display_selection_speed_body();
}


void find_optimal_selection_speed() {
  if (snab_mode == true) {
    if (temp_cube.get_avg() > upper_valve_selection_speed_table[selection_speed_body][2]) {
      selection_speed_body_decrease();
    }
  }
  if (rekt_mode == true) {
    if (temp_cube.get_avg() > lower_valve_selection_speed_table[selection_speed_body][2]) {
      selection_speed_body_decrease();
    }
  }
}

void open_upper_valve() {
  digitalWrite(UPPER_VALVE_PIN, 1);
  upper_valve_is_open = true;
  Serial.print("Upper valve opened at: ");
  Serial.println(current_time);
}

void close_upper_valve() {
  digitalWrite(UPPER_VALVE_PIN, 0);
  upper_valve_is_open = false;
  Serial.print("Upper valve closed at: ");
  Serial.println(current_time);
}

void upper_valve_operation(int speed) {
  if (stabilization_mode == false) {
    if ((current_time % 30000) < upper_valve_selection_speed_table[speed][1]) {
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

void lower_valve_operation(int speed) {
  if (stabilization_mode == false) {
    if ((current_time % 30000) < lower_valve_selection_speed_table[speed][1]) {
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

void open_lower_valve() {
  digitalWrite(LOWER_VALVE_PIN, 1);
  lower_valve_is_open = true;
  Serial.print("Lower valve opened at: ");
  Serial.println(current_time);
}

void close_lower_valve() {
  digitalWrite(LOWER_VALVE_PIN, 0);
  lower_valve_is_open = false;
  Serial.print("Lower valve closed at: ");
  Serial.println(current_time);
}



void turn_on_stabilization_mode() {
  if (stabilization_mode == false) {
    close_upper_valve();
    close_lower_valve();
    Serial.println("stabilization_mode is on");
    under_operation = false;
    stabilization_mode = true;
    stabilization_mode_time_started = current_time;
    fails_total++;
    lcd_display_fails_total();
    lcd_display_stabilization_mode();
  }
}

void turn_off_stabilization_mode() {
  if (stabilization_mode == true) {
    if (temp_in_pipe_more_than_fixed() == false) {
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
  if (return_temp_diff_fact() > temp_diff) {
    return true;
  } else {
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
    selection_speed_head = upper_valve_selection_speed_table_len - 4;
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
    lcd_set_display_in_operation_update();
  }
}

void scenario_snabby_body() {
  if (scenario_snabby_body_operation == false) {
    update_temp_fixed();
    scenario_snabby_body_operation = true;
    under_operation = true;
    selection_speed_body = 0;
    temp_diff = 30;
    decrease_lim = 29;
    Serial.println("scenario_snabby_body started");
    lcd_set_display_in_operation_update();
  }
  if (error_mode == false) {
    find_optimal_selection_speed();
    update_temp_fixed_at_91();
    check_temp_and_start_stabilization_mode_if_temp_not_ok();
    if (temp_cube.get_avg() < 9900) {
      upper_valve_operation(selection_speed_body);
    } else {
      close_upper_valve();
      finished = true;
      confirm_start = false;
      scenario_snabby_body_operation = false;
      under_operation = false;
      lcd_set_display_in_operation_update();
    }
  } else {
    close_upper_valve();
  }
}

void scenario_rekt_head() {
  if (scenario_rekt_head_operation == false) {
    operation_time_started = current_time;
    selection_speed_head = upper_valve_selection_speed_table_len - 3;
    scenario_rekt_head_operation = true;
    under_operation = true;
    mode_timer.update_init(current_time, rekt_head_mode_duration);
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
    lcd_set_display_in_operation_update();
  }
}

void scenario_rekt_body() {
  if (scenario_rekt_body_operation == false) {
    update_temp_fixed();
    scenario_rekt_body_operation = true;
    under_operation = true;
    selection_speed_body = 0;
    temp_diff = 20;
    decrease_lim = 35;
    mode_timer.update_init(current_time, rekt_head_mode_duration);
    selection_speed_head = upper_valve_selection_speed_table_len - 1;
    Serial.println("scenario_rekt_body started");
    lcd_set_display_in_operation_update();
  }
  if (error_mode == false) {
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
  } else {
    close_lower_valve();
    close_upper_valve();
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
    lower_valve_operation(selection_speed_body);
  } else {
    close_upper_valve();
    Serial.println("scenario_test finished");
    scenario_test_operation = false;
    finished = true;
    confirm_start = false;
    under_operation = false;
  }
}

void implement_mode() {
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


// показать температуру на дисплеях
unsigned long last_time_display_upd = 0;
int general_blink_delay = 2000;
int selection_in_menu_blink_delay = general_blink_delay / 2;
int selection_blink_delay = selection_in_menu_blink_delay / 2;



#define lcd_operation_status_coordinates 5, 0
#define lcd_stabilization_status_coordinates 5, 1
#define lcd_temp_pipe_coordinates 5, 2
#define lcd_temp_cube_coordindates 5, 3

#define lcd_main_mode_coordinates 0, 0
#define lcd_sub_mode_coordinates 0, 1
#define lcd_selection_speed_head_coordinates 0, 2
#define lcd_selection_speed_body_coordinates 0, 3

#define lcd_fails_coordinates 12, 3
#define lcd_time_passed_coordinates 12, 0
#define lcd_time_remains_coordinates 12, 1
#define lcd_temp_fixed_coordinates 12, 2
#define lcd_temp_diff_coordinates 18, 2



void lcd_display_start_screeen() {
  lcd.setCursor(7, 1);
  lcd.print("START MODE");
  lcd.setCursor(6, 2);
  lcd.print("Hold SELECT");
}

void lcd_display_fails_total() {
  lcd.setCursor(lcd_fails_coordinates);
  lcd.print(fails_total);
}

// switch (get_analog_val_3()) {
//   case 1: return "Snab"; break;
//   case 2: return "Rect"; break;
//   case 3: return "Test"; break;
// }

void lcd_display_temp_diff() {
  if (body_mode == true) {
    if (under_operation == true or stabilization_mode == true) {
      lcd.setCursor(lcd_temp_diff_coordinates);
      switch (return_temp_diff_fact()) {
        case -9999 ... - 10: lcd.print("<<"); break;
        case -9 ... - 1: lcd.print(return_temp_diff_fact()); break;
        case 0 ... 9:
          lcd.print(" ");
          lcd.print(return_temp_diff_fact());
          break;
        case 10 ... 99: lcd.print(return_temp_diff_fact()); break;
        case 100 ... 9999:
          lcd.print(">>");
          // }
          // if (return_temp_diff_fact() >= 0) {
          //   lcd.setCursor(lcd_temp_diff_coordinates);
          //   if (return_temp_diff_fact() < 100) {
          //     if (return_temp_diff_fact() < 10) {
          //       lcd.print(" ");
          //     }
          //     lcd.print(return_temp_diff_fact());
          //   } else {
          //     lcd.print(99);
          //   }
      }
    }
  }
}

void lcd_display_temp_fixed() {
  lcd.setCursor(lcd_temp_fixed_coordinates);
  lcd.print(temp_fixed / 100);
  lcd.print(",");
  if (temp_fixed % 100 < 10) {
    lcd.print(0);
  }
  lcd.print(temp_fixed % 100);
}

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
  if (error_mode == false) {
    if (temp_pipe.get_avg() / 100 < 10) {
      lcd.print(" ");
    }
    lcd.print(temp_pipe.get_avg() / 100);
    lcd.print(",");
    if (temp_pipe.get_avg() % 100 < 10) {
      lcd.print(0);
    }
    lcd.print(temp_pipe.get_avg() % 100);
  } else {
    lcd.print("Error");
  }
}

void lcd_display_temp_cube() {
  lcd.setCursor(lcd_temp_cube_coordindates);
  if (error_mode == false) {
    if (temp_cube.get_avg() / 100 < 10) {
      lcd.print(" ");
    }
    lcd.print(temp_cube.get_avg() / 100);
    lcd.print(",");
    if (temp_cube.get_avg() % 100 < 10) {
      lcd.print(0);
    }
    lcd.print(temp_cube.get_avg() % 100);
  } else {
    lcd.print("Error");
  }
}

void lcd_set_display_in_operation_general() {
  lcd_display_main_mode_in_operation();
  lcd_display_sub_mode_in_operation();
  lcd_display_operation_status();
}

void clear_display() {
  for (int i = 0; i < 4; i++) {
    lcd.setCursor(0, i);
    lcd.print("                    ");
  }
}

void lcd_set_display_in_operation_update() {
  lcd_display_selection_speed_head();
  lcd_display_selection_speed_body();
  lcd_display_operation_status();
  lcd_display_stabilization_mode();
  lcd_display_temp_fixed();
}

void lcd_display_temperatures() {
  if (operation_mode == true) {
    lcd_display_temp_pipe();
    lcd_display_temp_cube();
    lcd_display_temp_diff();
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
  lcd.print(lcd_sub_mode_text());
}

void lcd_display_main_mode_in_selection() {
  lcd.setCursor(lcd_main_mode_coordinates);
  lcd.print(lcd_display_main_mode_in_selection_text());
}

void lcd_display_timer() {
  if (under_operation == true) {
    unsigned long started = millis();
    lcd.setCursor(lcd_time_passed_coordinates);
    lcd.print(mode_timer.return_hours_passed());
    lcd.print(":");
    if (mode_timer.return_minutes_passed() < 10) {
      lcd.print("0");
    }
    lcd.print(mode_timer.return_minutes_passed());
    lcd.print(":");
    if (mode_timer.return_seconds_passed() < 10) {
      lcd.print("0");
    }
    lcd.print(mode_timer.return_seconds_passed());
  //   lcd.setCursor(lcd_time_remains_coordinates);
  //   lcd.print(mode_timer.return_hours_remains());
  //   lcd.print(":");
  //   if (mode_timer.return_minutes_remains() < 10) {
  //     lcd.print("0");
  //   }
  //   lcd.print(mode_timer.return_minutes_remains());
  //   lcd.print(":");
  //   if (mode_timer.return_seconds_remains() < 10) {
  //     lcd.print("0");
  //   }
  //   lcd.print(mode_timer.return_seconds_remains());
  Serial.println(millis() - started);
  }
}

void lcd_display_data() {
  if (selection_mode == true) {
    if (current_time - last_time_display_upd > 100) {
      lcd_update_data_in_selection();
      last_time_display_upd = current_time;
    }
  } else if (operation_mode == true) {
    if (current_time - last_time_display_upd > 750) {
      lcd_display_temperatures();
      last_time_display_upd = current_time;
    }
  }
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

void lcd_display_selection_speed_head() {
  lcd.setCursor(lcd_selection_speed_head_coordinates);
  if (head_mode == true or rekt_mode == true) {
    if (upper_valve_selection_speed_table[selection_speed_head][0] < 100) {
      lcd.print("  ");
    } else if (upper_valve_selection_speed_table[selection_speed_head][0] < 1000) {
      lcd.print(" ");
    }
    lcd.print(upper_valve_selection_speed_table[selection_speed_head][0]);
  } else {
    lcd.print("----");
  }
}

void lcd_display_selection_speed_body() {
  lcd.setCursor(lcd_selection_speed_body_coordinates);
  if (body_mode == true) {
    if (snab_mode == true) {
      if (upper_valve_selection_speed_table[selection_speed_body][0] < 1000) {
        lcd.print(" ");
      }
      lcd.print(upper_valve_selection_speed_table[selection_speed_body][0]);
    } else if (rekt_mode == true) {
      if (lower_valve_selection_speed_table[selection_speed_body][0] < 1000) {
        lcd.print(" ");
      }
      lcd.print(lower_valve_selection_speed_table[selection_speed_body][0]);
    }
  } else {
    lcd.print("----");
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

String lcd_00_00_selection_mode_main_menu() {
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

String lcd_00_01_selection_mode_main_menu() {
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

String lcd_sub_mode_text() {
  if (sub_mode_selection == true) {
    return lcd_00_01_selection_mode_sub_mode_selection();
  } else {
    return lcd_00_01_selection_mode_main_menu();
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
  selection_speed_body_decrease();
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
  lcd_set_display_in_operation_general();
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
    clear_display();
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

  lcd_display_start_screeen();
  Watchdog.enable(RESET_MODE, WDT_PRESCALER_128);
}

void loop() {
  current_time = millis();
  main_button.tick();
  service_button.tick();
  operation();
  write_temp_and_request_new();
  update_timer();
  change_mode();
  lcd_display_data();
  lcd.setCursor(15, 3);
  if (error_mode == true) {
    lcd.print("error");
  } else {
    lcd.print("ookkk");
  }
  // mode_timer.print();
  Watchdog.reset();
}