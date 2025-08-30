# everblu-meters - Water usage data for Home Assistant
Fetch water/gas usage data from Cyble EverBlu meters using RADIAN protocol on 433Mhz. Fully integrated with Home Assistant via MQTT, including auto-discovery for all sensors.

Meters supported:
- Itron EverBlu Cyble Enhanced

# WARNING!!!

Others have noted that the everblu meter will retain a count of *total number of queries* against the meter. Some water companies do not know what is happening when this number increases at a fast rate and will assume the meter has gone bad and replace it.

See reports here - https://community.home-assistant.io/t/reading-itron-everblu-cyble-rf-enhanced-water-meter-with-esp32-esp8266-and-433mhz-cc1101-home-assistant-mqtt-autodiscovery-now-with-rssi-and-more/833180

## Battery
The meter has an internal battery, which should last for 10 years when queried once a day.

## Hardware
![Raspberry Pi Zero with CC1101](board.jpg)
The project runs on Raspberry Pi with an RF transreciver (CC1101).

### Connections (rpi to CC1101):
- pin 1 (3V3) to pin 2 (VCC)
- pin 6 (GND) to pin 1 (GND)
- pin 11 (GPIO0	) to pin 3 (GDO0)
- pin 24 (CE0) to pin 4 (CSN)
- pin 23 (SCLK) to pin 5 (SCK)
- pin 19 (MOSI) to pin 6 (MOSI)
- pin 21 (MISO) to pin 7 (MISO)
- pin 13 (GPIO27) to pin 8 (GD02)


## Configuration
1.  Enable SPI in `raspi-config`.
2.  Install WiringPi from https://github.com/WiringPi/WiringPi/
3.  Install `libmosquitto-dev`: `apt install libmosquitto-dev`
4.  Create a configuration file named `everblu_meters.conf` by copying the example provided in the repository. Edit this file with your meter and MQTT details. It should look like this:
    ```ini
    # everblu_meters.conf
    METER_YEAR=21
    METER_SERIAL=123456
    MQTT_HOST=192.168.1.100
    MQTT_PORT=1883
    MQTT_USER=your_mqtt_user
    MQTT_PASS=your_mqtt_password
    DEVICE_NAME=My Water Meter (this is what the entity will be found as)
    ```
5.  Compile the code with `make`.
6.  Run the program, passing the path to your configuration file:
    ```bash
    ./everblu_meters everblu_meters.conf
    ```
    After a few seconds, your meter data will appear on the screen and be published to MQTT.
7.  Setup crontab to run it on a schedule (e.g., once or twice a day).

## MQTT and Home Assistant Integration
The program now fully supports Home Assistant's MQTT auto-discovery protocol. When you run the application for the first time, it will automatically create and configure a new device in Home Assistant with the following sensors:
-   **Water Meter (Liters):** The main water consumption reading.
-   **Water Meter Battery:** The remaining battery life in months.
-   **Water Meter RSSI:** The signal strength of the meter's radio transmission in dBm.
-   **Water Meter Read Counter:** The total number of times the meter has been queried.
-   **Water Meter Wake Time:** The hour the meter begins listening for requests (e.g., 08:00).
-   **Water Meter Sleep Time:** The hour the meter stops listening for requests (e.g., 16:00).

All sensor data is published to topics under `everblu/cyblemeter_<year>_<serial>/`.

## Troubleshooting

### Frequency adjustment
Your transreciver module may be not calibrated correctly, please modify frequency a bit lower or higher and try again. You may use RTL-SDR to measure the offset needed.


### Business hours
Your meter may be configured in such a way that is listens for request only during hours when data collectors work - to conserve energy. If you are unable to communicate with the meter, please try again during business hours (8-16).

### Serial number starting with 0
Please ignore the leading 0, provide serial in configuration without it e.g. 0123456 should be 123456

### Save power
The meter has internal battery, which should last for 10 years when queried once a day. The metrics should be displayed for what 

## Origin and license

This code is based on code from http://www.lamaisonsimon.fr/wiki/doku.php?id=maison2:compteur_d_eau:compteur_d_eau


The license is unknown, citing one of the authors (fred):

> I didn't put a license on this code maybe I should, I didn't know much about it in terms of licensing.
> this code was made by "looking" at the radian protocol which is said to be open source earlier in the page, I don't know if that helps?

# Links

There is a very nice port to ESP8266/ESP32: https://github.com/psykokwak-com/everblu-meters-esp8266 (shamelessly stolen some things from here for this fork)