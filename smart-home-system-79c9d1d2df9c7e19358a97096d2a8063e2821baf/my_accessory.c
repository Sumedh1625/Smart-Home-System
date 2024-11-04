#include <homekit/homekit.h>
#include <homekit/characteristics.h>

char serial_num[8] = "XXXX\0";
char bridge_name[20] = "SB31-XXXX\0";
#define appliance1_name "Appliance 1"
#define appliance2_name "Appliance 2"
#define appliance3_name "Appliance 3"
#define appliance4_name "Fan"

void my_accessory_identify(homekit_value_t _value) {
//  printf("accessory identify\n");
}

homekit_characteristic_t cha_switch1_on = HOMEKIT_CHARACTERISTIC_(ON, false);
homekit_characteristic_t cha_switch2_on = HOMEKIT_CHARACTERISTIC_(ON, false);
homekit_characteristic_t cha_switch3_on = HOMEKIT_CHARACTERISTIC_(ON, false);
homekit_characteristic_t cha_fan_active = HOMEKIT_CHARACTERISTIC_(ACTIVE, 0);
homekit_characteristic_t cha_fan_rotation_speed = HOMEKIT_CHARACTERISTIC_(ROTATION_SPEED, 10.0);

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_bridge, .services=(homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, bridge_name),
            HOMEKIT_CHARACTERISTIC(MANUFACTURER, "ASAP"),
            HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, serial_num),
            HOMEKIT_CHARACTERISTIC(MODEL, "SB4F"),
            HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "1.0"),
            HOMEKIT_CHARACTERISTIC(IDENTIFY, my_accessory_identify),
            NULL
        }),
        NULL
    }),
  HOMEKIT_ACCESSORY(.id=2, .category=homekit_accessory_category_switch, .services=(homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, "Appliance 1"),
      HOMEKIT_CHARACTERISTIC(IDENTIFY, my_accessory_identify),
      NULL
    }),
      HOMEKIT_SERVICE(SWITCH, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, appliance1_name),
      &cha_switch1_on,
      NULL
    }),
    NULL
  }),
  HOMEKIT_ACCESSORY(.id=3, .category=homekit_accessory_category_switch, .services=(homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, "Appliance 2"),
      HOMEKIT_CHARACTERISTIC(IDENTIFY, my_accessory_identify),
      NULL
    }),
      HOMEKIT_SERVICE(SWITCH, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, appliance2_name),
      &cha_switch2_on,
      NULL
    }),
    NULL
  }),
  HOMEKIT_ACCESSORY(.id=4, .category=homekit_accessory_category_switch, .services=(homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, "Appliance 3"),
      HOMEKIT_CHARACTERISTIC(IDENTIFY, my_accessory_identify),
      NULL
    }),
      HOMEKIT_SERVICE(SWITCH, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, appliance3_name),
      &cha_switch3_on,
      NULL
    }),
    NULL
  }),
  HOMEKIT_ACCESSORY(.id=5, .category=homekit_accessory_category_fan, .services=(homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, "Fan"),
      HOMEKIT_CHARACTERISTIC(IDENTIFY, my_accessory_identify),
      NULL
    }),
      HOMEKIT_SERVICE(FAN2, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, appliance4_name),
      &cha_fan_active,
      &cha_fan_rotation_speed,
      NULL
    }),
    NULL
  }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111"
};
