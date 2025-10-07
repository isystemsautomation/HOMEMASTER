substitutions:
  # General metadata and variables for reuse in the config
  name: "opentherm"                # Device hostname in ESPHome / network
  friendly_name: "Homemaster Opentherm Gateway"  # Friendly name in Home Assistant UI
  room: ""                                   # Optional: assign to a room in HA
  device_description: "Homemaster Opentherm Gateway" # Description for metadata
  project_name: "Homemaster.Opentherm Gateway" # Unique project identifier
  project_version: "v1.0.0"                  # Firmware version
  update_interval: 60s                       # Default sensor update frequency
  dns_domain: ".local"                       # mDNS domain suffix
  timezone: ""                               # Timezone (if needed different from HA server)
  wifi_fast_connect: "false"                 # Faster reconnect if true (skips scan)
  log_level: "DEBUG"                         # Logging level
  ipv6_enable: "false"                       # IPv6 support toggle

esphome:
  # Device-level settings for ESPHome
  name: "${name}"
  friendly_name: "${friendly_name}"
  comment: "${device_description}"
  area: "${room}"
  name_add_mac_suffix: true                  # Append MAC to hostname to avoid duplicates
  min_version: 2025.7.0                      # Minimum ESPHome version required
  project:
    name: "${project_name}"
    version: "${project_version}"

esp32:
  # Target hardware platform
  board: esp32dev
  framework:
    type: esp-idf                            # Use Espressif IDF framework
    version: recommended

logger:
  baud_rate: 115200                          # Serial log speed
  level: ${log_level}                        # Log level set from substitutions

mdns:
  disabled: false                            # Enable mDNS for network discovery

api:                                         # Enable native ESPHome <-> Home Assistant API

ota:
  - platform: esphome
    id: ota_esphome                          # Over-the-air updates

network:
  enable_ipv6: ${ipv6_enable}                # Enable/disable IPv6

wifi:
  ap: {}                                     # Fallback AP for first-time setup
  fast_connect: "${wifi_fast_connect}"       # Quick reconnect option
  domain: "${dns_domain}"                    # mDNS suffix

captive_portal:                              # Captive portal for AP fallback

improv_serial:
  id: improv_serial_if                       # Enable Improv setup over serial

esp32_improv:
  authorizer: none
  id: improv_ble_if                          # Enable Improv setup over BLE

dashboard_import:
  # Auto-import official config from GitHub into ESPHome Dashboard
  package_import_url: github://isystemsautomation/HOMEMASTER/OpenthermGateway/Firmware/opentherm.yaml@main
  import_full_config: true

opentherm:
  id: ot_bus                                 # OpenTherm bus definition
  in_pin: 21                                 # GPIO for receiving OpenTherm signal
  out_pin: 26                                # GPIO for sending OpenTherm signal

# Local button on GPIO35
binary_sensor:
  - platform: gpio
    id: bs_button_1
    name: "Button #1"
    pin: GPIO35

switch:
# Local relay output
  - platform: gpio
    id: sw_relay
    pin: GPIO32
    name: "RELAY"

status_led:
  pin:
    number: GPIO33                          # Status LED pin
    inverted: true                          # LED is active-low