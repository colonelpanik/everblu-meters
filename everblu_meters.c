 /*  the radian_trx SW shall not be distributed  nor used for commercial product*/
 /*  it is exposed just to demonstrate CC1101 capability to reader water meter indexes */

#include <stdio.h>
#include <stdlib.h>
#include <mosquitto.h>
#include <string.h>
#include <ctype.h>

#include "everblu_meters.h"

struct AppConfig
{
    int meter_year;
    int meter_serial;
    char mqtt_host[256];
    int mqtt_port;
    char mqtt_user[256];
    char mqtt_pass[256];
};

// Global config variable to be accessible by the included cc1101.c
struct AppConfig config; 
// The following macros are now replaced by the global 'config' struct.
// This is necessary because cc1101.c is included directly and its functions
// need access to these values.
#define METER_YEAR config.meter_year
#define METER_SERIAL config.meter_serial
#define MQTT_KEEP_ALIVE 60
#define MQTT_MSG_MAX_SIZE  512

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
	if (file == NULL)
    if (file == NULL) {
        // Use perror to print the system error message (e.g., "No such file or directory")
        perror("Error opening configuration file");
        // We will continue with defaults, but you could exit here if a config file is mandatory
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
        if (sscanf(line, "%[^=]=%s", key, value) == 2)
		{
            trim(key);
            trim(value);
            if (strcmp(key, "METER_YEAR") == 0) config->meter_year = atoi(value);
            else if (strcmp(key, "METER_SERIAL") == 0) config->meter_serial = atoi(value);
            else if (strcmp(key, "MQTT_HOST") == 0) strncpy(config->mqtt_host, value, sizeof(config->mqtt_host) - 1);
            else if (strcmp(key, "MQTT_PORT") == 0) config->mqtt_port = atoi(value);
            else if (strcmp(key, "MQTT_USER") == 0) strncpy(config->mqtt_user, value, sizeof(config->mqtt_user) - 1);
            else if (strcmp(key, "MQTT_PASS") == 0) strncpy(config->mqtt_pass, value, sizeof(config->mqtt_pass) - 1);
        }
    }
    fclose(file);
}

void my_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{

	if(message->payloadlen)
	{
		printf("%s %s", message->topic, (char *)message->payload);
	}
	else
	{
		//printf("%s (null)\n", message->topic);
	}
	fflush(stdout);
}

void my_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
	if(!result)
	{
		/* Subscribe to broker information topics on successful connect. */
		mosquitto_subscribe(mosq, NULL, "WaterUsage ", 2);
	}
	else
	{
		fprintf(stderr, "Connect failed\n");
	}
}

void my_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
	int i;
	printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for(i=1; i<qos_count; i++)
	{
		printf(", %d", granted_qos[i]);
	}
	printf("\n");
}

void my_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
	/* Pring all log messages regardless of level. */
	printf("%s\n", str);

}


void IO_init(void)
{
	wiringPiSetup();
	pinMode (GDO2, INPUT);
	pinMode (GDO0, INPUT);           

	cc1101_init();
}


nt main(int argc, char *argv[])
{
	struct tmeter_data meter_data;
	struct mosquitto *mosq = NULL;
	char buff[MQTT_MSG_MAX_SIZE];
	char meter_id[12];
	char mqtt_topic[64];

	if (argc < 2)
	{
        fprintf(stderr, "Usage: %s <path_to_config_file>\n", argv[0]);
        return 1; // Exit with an error
    }
    const char *config_filename = argv[1];

    // --- Load Configuration ---
    // 1. Set hardcoded default values
    config.meter_year = 16;
    config.meter_serial = 123456;
    strncpy(config.mqtt_host, "localhost", sizeof(config.mqtt_host));
    config.mqtt_port = 1883;
    strncpy(config.mqtt_user, "homeassistant", sizeof(config.mqtt_user));
    strncpy(config.mqtt_pass, "PASS", sizeof(config.mqtt_pass));
    
    // 2. Override defaults with values from the config file
    parse_config(config_filename);
	    
    // Print the configuration being used
    printf("--- Using Configuration ---\n");
    printf("Meter: %d-%d\n", config.meter_year, config.meter_serial);
    printf("MQTT Broker: %s:%d\n", config.mqtt_host, config.mqtt_port);
    printf("MQTT User: %s\n", config.mqtt_user);
    printf("---------------------------\n");


	sprintf(meter_id, "%i_%i", config.meter_year, config.meter_serial);
	
	mosquitto_lib_init();
	mosq = mosquitto_new(NULL, true, NULL);
	if(!mosq)
	{
		fprintf(stderr, "ERROR: Create MQTT client failed..\n");
		mosquitto_lib_cleanup();
		return 1;
	}
	
	//Set callback functions
	mosquitto_log_callback_set(mosq, my_log_callback);
	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_message_callback_set(mosq, my_message_callback);
	mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);

	mosquitto_username_pw_set(mosq, config.mqtt_user, config.mqtt_pass);

	//Connect to MQTT server
	if(mosquitto_connect(mosq, config.mqtt_host, config.mqtt_port, MQTT_KEEP_ALIVE))
	{
		fprintf(stderr, "ERROR: Unable to connect to MQTT broker.\n");
		return 1;
	}

	//Start a thread, and call mosquitto loop() continuously in the thread to process network information
	int loop = mosquitto_loop_start(mosq);
	if(loop != MOSQ_ERR_SUCCESS)
	{
		fprintf(stderr, "ERROR: failed to create mosquitto loop");
		return 1;
	}

	IO_init();
	do 
	{
        // get_meter_data() will use the global config struct for METER_YEAR and METER_SERIAL
		meter_data = get_meter_data();
		if (meter_data.liters < 0) 
		{
			fprintf(stderr, "Invalid meter reading received. Retrying in 5 seconds...\n");
			sleep(5); // Wait for 5 seconds before trying again
		}
	} while (meter_data.liters < 0);

	sprintf(buff, "%d", meter_data.liters);
	sprintf(mqtt_topic, "homeassistant/sensor/cyblemeter_%s/state", meter_id);

	printf("Liters: %i\n", meter_data.liters);
	mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,false);

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	return 0;
}
