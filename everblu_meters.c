/*
 * everblu_meters.c - Enhanced MQTT and Home Assistant Integration
*/

#include <stdio.h>
#include <stdlib.h>
#include <mosquitto.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "everblu_meters.h"

// Configuration and User Data Structures
struct AppConfig
{
    int meter_year;
    int meter_serial;
    char mqtt_host[256];
    int mqtt_port;
    char mqtt_user[256];
    char mqtt_pass[256];
    char device_name[64];
};

struct MosquittoUserData {
    char meter_id[32];
    char device_name[64];
    char base_topic[128];
};

// Global config variable to be accessible by the included cc1101.c
struct AppConfig config;
#define METER_YEAR config.meter_year
#define METER_SERIAL config.meter_serial
#define MQTT_KEEP_ALIVE 60
#define MQTT_MSG_MAX_SIZE  1024

#include "cc1101.c"


void trim(char *str)
{
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

void parse_config(const char *filename, struct AppConfig *config)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening configuration file");
        fprintf(stderr, "Warning: Could not open '%s'. Using default settings.\n", filename);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), file))
	{
		// Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == ';')
		{
            continue;
        }
        char key[256], value[256];
        if (sscanf(line, " %255[^ =] = %255[^\n]", key, value) == 2)
		{
            trim(key);
            trim(value);
            if (strcmp(key, "METER_YEAR") == 0) config->meter_year = atoi(value);
            else if (strcmp(key, "METER_SERIAL") == 0) config->meter_serial = atoi(value);
            else if (strcmp(key, "MQTT_HOST") == 0) strncpy(config->mqtt_host, value, sizeof(config->mqtt_host) - 1);
            else if (strcmp(key, "MQTT_PORT") == 0) config->mqtt_port = atoi(value);
            else if (strcmp(key, "MQTT_USER") == 0) strncpy(config->mqtt_user, value, sizeof(config->mqtt_user) - 1);
            else if (strcmp(key, "MQTT_PASS") == 0) strncpy(config->mqtt_pass, value, sizeof(config->mqtt_pass) - 1);
            else if (strcmp(key, "DEVICE_NAME") == 0) strncpy(config->device_name, value, sizeof(config->device_name) - 1);
        }
    }
    fclose(file);
}

// Helper function to publish a message and check for errors
void publish_mqtt(struct mosquitto *mosq, const char *topic, const char *payload, bool retain) {
    // Use QoS 0 for reliability in short-lived scripts, and set retain flag.
    int rc = mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 0, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error publishing to topic %s: %s\n", topic, mosquitto_strerror(rc));
    } else {
        printf("Published to %s: %s\n", topic, payload);
    }
}


void publish_hass_autodiscovery(struct mosquitto *mosq, struct MosquittoUserData *ud) {
    char topic[256];
    char payload[1024];

    const char* device_json_template = "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mdl\":\"Itron EverBlu (RPI)\",\"mf\":\"Itron\"}";
    const char* availability_json_template = "\"avty_t\":\"%s/status\"";

    char device_str[256];
    snprintf(device_str, sizeof(device_str), device_json_template, ud->meter_id, ud->device_name);

    char avail_str[256];
    snprintf(avail_str, sizeof(avail_str), availability_json_template, ud->base_topic);

    // 1. Liters Sensor
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_liters/config", ud->meter_id);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\",\"uniq_id\":\"%s_liters\",\"ic\":\"mdi:water\","
        "\"unit_of_meas\":\"L\",\"dev_cla\":\"water\",\"stat_cla\":\"total_increasing\","
        "\"stat_t\":\"%s/liters\",%s,%s}",
        ud->device_name, ud->meter_id, ud->base_topic, avail_str, device_str);
    publish_mqtt(mosq, topic, payload, true);

    // 2. Battery Sensor
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_battery/config", ud->meter_id);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s Battery\",\"uniq_id\":\"%s_battery\",\"dev_cla\":\"battery\","
        "\"unit_of_meas\":\"Months\",\"stat_t\":\"%s/battery\",\"ent_cat\":\"diagnostic\",%s,%s}",
        ud->device_name, ud->meter_id, ud->base_topic, avail_str, device_str);
    publish_mqtt(mosq, topic, payload, true);

    // 3. RSSI Sensor
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_rssi/config", ud->meter_id);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s RSSI\",\"uniq_id\":\"%s_rssi\",\"dev_cla\":\"signal_strength\","
        "\"unit_of_meas\":\"dBm\",\"stat_t\":\"%s/rssi_dbm\",\"ent_cat\":\"diagnostic\",%s,%s}",
        ud->device_name, ud->meter_id, ud->base_topic, avail_str, device_str);
    publish_mqtt(mosq, topic, payload, true);
    
    // 4. Counter Sensor
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_counter/config", ud->meter_id);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s Read Counter\",\"uniq_id\":\"%s_counter\",\"ic\":\"mdi:counter\","
        "\"stat_t\":\"%s/counter\",\"ent_cat\":\"diagnostic\",%s,%s}",
        ud->device_name, ud->meter_id, ud->base_topic, avail_str, device_str);
    publish_mqtt(mosq, topic, payload, true);
        
    // 5. Time Start Sensor
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_time_start/config", ud->meter_id);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s Wake Time\",\"uniq_id\":\"%s_time_start\",\"ic\":\"mdi:timer-sand\","
        "\"stat_t\":\"%s/time_start\",\"ent_cat\":\"diagnostic\",%s,%s}",
        ud->device_name, ud->meter_id, ud->base_topic, avail_str, device_str);
    publish_mqtt(mosq, topic, payload, true);

    // 6. Time End Sensor
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_time_end/config", ud->meter_id);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s Sleep Time\",\"uniq_id\":\"%s_time_end\",\"ic\":\"mdi:timer-sand-off\","
        "\"stat_t\":\"%s/time_end\",\"ent_cat\":\"diagnostic\",%s,%s}",
        ud->device_name, ud->meter_id, ud->base_topic, avail_str, device_str);
    publish_mqtt(mosq, topic, payload, true);
        
    printf("Published Home Assistant auto-discovery configurations.\n");
}


void my_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
	if(!result)
	{
		printf("Connected to MQTT Broker.\n");
        struct MosquittoUserData *ud = (struct MosquittoUserData *)userdata;
		publish_hass_autodiscovery(mosq, ud);
        
        char status_topic[256];
        snprintf(status_topic, sizeof(status_topic), "%s/status", ud->base_topic);
        publish_mqtt(mosq, status_topic, "online", true);
	}
	else
	{
		fprintf(stderr, "Connect failed: %s\n", mosquitto_connack_string(result));
	}
}


void IO_init(void)
{
	wiringPiSetup();
	pinMode (GDO2, INPUT);
	pinMode (GDO0, INPUT);           
	cc1101_init();
}


int main(int argc, char *argv[])
{
	struct tmeter_data meter_data;
	struct mosquitto *mosq = NULL;
    struct MosquittoUserData userdata;

	if (argc < 2)
	{
        fprintf(stderr, "Usage: %s <path_to_config_file>\n", argv[0]);
        return 1;
    }
    const char *config_filename = argv[1];

    // --- Load Configuration ---
    strncpy(config.device_name, "Water Meter", sizeof(config.device_name)); // Default name
    parse_config(config_filename, &config);
	    
    printf("--- Using Configuration ---\n");
    printf("Meter: %d-%d\n", config.meter_year, config.meter_serial);
    printf("MQTT Broker: %s:%d\n", config.mqtt_host, config.mqtt_port);
    printf("MQTT User: %s\n", config.mqtt_user);
    printf("Device Name: %s\n", config.device_name);
    printf("---------------------------\n");

    // --- Setup MQTT ---
	snprintf(userdata.meter_id, sizeof(userdata.meter_id), "cyblemeter_%d_%d", config.meter_year, config.meter_serial);
    strncpy(userdata.device_name, config.device_name, sizeof(userdata.device_name)-1);
    snprintf(userdata.base_topic, sizeof(userdata.base_topic), "everblu/%s", userdata.meter_id);
	
	mosquitto_lib_init();
	mosq = mosquitto_new(userdata.meter_id, true, &userdata);
	if(!mosq) {
		fprintf(stderr, "ERROR: Create MQTT client failed.\n");
		return 1;
	}
	
	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_username_pw_set(mosq, config.mqtt_user, config.mqtt_pass);

    char status_topic[256];
    snprintf(status_topic, sizeof(status_topic), "%s/status", userdata.base_topic);
    mosquitto_will_set(mosq, status_topic, strlen("offline"), "offline", 0, true);

	if(mosquitto_connect(mosq, config.mqtt_host, config.mqtt_port, MQTT_KEEP_ALIVE)) {
		fprintf(stderr, "ERROR: Unable to connect to MQTT broker.\n");
		return 1;
	}

	if(mosquitto_loop_start(mosq) != MOSQ_ERR_SUCCESS) {
		fprintf(stderr, "ERROR: failed to start mosquitto loop.\n");
		return 1;
	}
    
    // Give time for connection and discovery messages to be sent
    sleep(2);

    // --- Read Meter ---
	IO_init();
    printf("Attempting to read meter...\n");
	do 
	{
		meter_data = get_meter_data();
		if (meter_data.liters < 0) 
		{
			fprintf(stderr, "Invalid meter reading received. Retrying in 5 seconds...\n");
			sleep(5);
		}
	} while (meter_data.liters < 0);

    // --- Publish Data ---
    printf("\n--- Meter Data Acquired ---\n");
    printf("Liters: %d\n", meter_data.liters);
    printf("Battery Left: %d months\n", meter_data.battery_left);
    printf("Read Counter: %d\n", meter_data.reads_counter);
    printf("RSSI: %.2f dBm\n", meter_data.rssi_dbm);
    printf("Wake/Sleep: %02d:00 - %02d:00\n", meter_data.time_start, meter_data.time_end);
    printf("---------------------------\n\n");
    printf("--- Publishing to MQTT ---\n");

    char topic[256];
    char payload[64];

    snprintf(topic, sizeof(topic), "%s/liters", userdata.base_topic);
    snprintf(payload, sizeof(payload), "%d", meter_data.liters);
    publish_mqtt(mosq, topic, payload, true);

    snprintf(topic, sizeof(topic), "%s/battery", userdata.base_topic);
    snprintf(payload, sizeof(payload), "%d", meter_data.battery_left);
    publish_mqtt(mosq, topic, payload, true);

    snprintf(topic, sizeof(topic), "%s/counter", userdata.base_topic);
    snprintf(payload, sizeof(payload), "%d", meter_data.reads_counter);
    publish_mqtt(mosq, topic, payload, true);

    snprintf(topic, sizeof(topic), "%s/rssi_dbm", userdata.base_topic);
    snprintf(payload, sizeof(payload), "%.2f", meter_data.rssi_dbm);
    publish_mqtt(mosq, topic, payload, true);

    snprintf(topic, sizeof(topic), "%s/time_start", userdata.base_topic);
    snprintf(payload, sizeof(payload), "%02d:00", meter_data.time_start);
    publish_mqtt(mosq, topic, payload, true);

    snprintf(topic, sizeof(topic), "%s/time_end", userdata.base_topic);
    snprintf(payload, sizeof(payload), "%02d:00", meter_data.time_end);
    publish_mqtt(mosq, topic, payload, true);

    time_t t = time(NULL);
    strftime(payload, sizeof(payload), "%Y-%m-%dT%H:%M:%S%z", localtime(&t));
    snprintf(topic, sizeof(topic), "%s/timestamp", userdata.base_topic);
    publish_mqtt(mosq, topic, payload, true);
    
    printf("---------------------------\n");

	sleep(3); // Give MQTT time to send all messages before exiting

	mosquitto_disconnect(mosq);
    mosquitto_loop_stop(mosq, true);
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	return 0;
}