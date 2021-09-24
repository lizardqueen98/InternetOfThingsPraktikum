#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#include "esp_log.h"

#include "ssd1306.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sntp.h"

#include <stdlib.h>

#include <stdint.h>
#include <stddef.h>
#include "esp_wifi.h"
#include "esp_netif.h"

#include "freertos/semphr.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "mqtt_client.h"
#include "commands.h"

#define BUTTON_TRIGGERED_GPIO_OUTSIDE 33 //CONFIG_BUTTON_1 //outside barrier
#define BUTTON_TRIGGERED_GPIO_INSIDE 4 //CONFIG_BUTTON_2 //inside barries

/*
This "int time = 2000" is the only proper way to specify 2000ms (or 2s), while in order to convert the numebr 
of ticks into milliseconds, we simply mutiply the number of ticks with the constant "portTICK_PERIOD_MS".
Example: 
(all the definitions below give output in milliseconds)
int time1 = 2000;
int time2 = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS; //used to measure time in ms from inside an ISR
int time3 = xTaskGetTickCount() * portTICK_PERIOD_MS; //used to measure time in ms when you are not in an ISR


The only time when we use the construction "number / portTICK_PERIOD_MS" is when we use the "vTaskDelay"
function, like: "vTaskDelay(1000/portTICK_PERIOD_MS)", which would make a delay of 1000ms, but don't confuse
this with the above explained way to get milliseconds!!!  
*/  
#define ALLOWED_TIME_IN_MIDDLE_STATE 115
#define TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION 9000
/*note that "FASTER_MOVEMENT_THAN_HUMAN" has to be smaller then the "ALLOWED_TIME_IN_MIDDLE_STATE", as 
 "ALLOWED_TIME_IN_MIDDLE_STATE" is considered to be the the most time a sensor needs to react to a movement of
 a human, but there are thing faster than humans (like bullets). So, a sensor on the other side from the one we 
 are coming from must react in the sapn of [FASTER_MOVEMENT_THAN_HUMAN, ALLOWED_TIME_IN_MIDDLE_STATE], which are
 bacially the fastest time a human being can break two sensors in a row, and the slowest a human can break two 
 sensors in the row. If the time is higher than one of these times, a person was just peaking from one side, and
 if the time is smaller than in the interval, then the moving object is not a human, but something faster (like a
 bullet).*/
#define FASTER_MOVEMENT_THAN_HUMAN 80

static const char *TAG = "example";

//"volatile" is not being optimised by the compiler
volatile uint8_t countOfPeopleInTheRoom = 0;
static RTC_NOINIT_ATTR int restartCount;

esp_mqtt_client_handle_t client_subscriber;
esp_mqtt_client_handle_t client_publisher;
//parameters needed for making a timer
esp_timer_handle_t myTimerHandler1, myTimerHandler2, myTimerHandler3;

void timerIncreaseDecreaseCountOfPeopleInTheRoom_callback(void *param);
void timerAvoidObstructionOfSensors_callback(void* param);
void publishEventClient_callback(void *param);

esp_timer_create_args_t timerUntilEnteringOrExitingRoom = {
  .callback = &timerIncreaseDecreaseCountOfPeopleInTheRoom_callback,
  .name = "a timer for room counter increasing/reducing"
  };

esp_timer_create_args_t timerForAvoidingObstruction = {
  .callback = &timerAvoidObstructionOfSensors_callback,
  .name = "a timer making sure that nobody or nothing obstructs the sensor"
  };

esp_timer_create_args_t publishClientTimer = {
  .callback = &publishEventClient_callback,
  .name = "trigger push every 15 mins"
  };

xQueueHandle displayQueueHandle;

enum roomCounterChange
{
  increase = 0,
  decrease = 1,
  impossibleChange = 2
};

enum state 
{
  startState = 0,
  
  enteringOutsideActiveInsideInactive = 1,
  enetringBothActive = 2,    //IMPORTANT: "enetringBothActive" is a state where somebody is standing horizontally
  enteringBothInactive  = 3, //IMPORTANT: "eneteringBothInactive" is a different satate from "startState"
  enteringOutsideInactiveInsideActive = 4,
  eneteringStateFinishingBothInactive = 5,

  exitingOutsideInactiveInsideActive = 6,
  exitingBothActive = 7,       //IMPORTANT: "exitingBothActive" is a state where somebody is standing horizontally
  exitingBothInactive  = 8, //IMPORTANT: "eneteringBothInactive" is a different satate from "startState" 
  exitingOutsideActiveInsideInactive = 9,  
  exitingStateFinishingBothInactive = 10    

} previousState, currentState;

enum action
{
  risingEdgeOutside = 0,
  fallingEdgeOutside = 1,

  risingEdgeInside = 2,
  fallingEdgeInside = 3
};

uint32_t lastTrigger_OutsideBarrierButton;
uint32_t lastTrigger_InsideBarrierButton;

xQueueHandle interrputQueue;

uint32_t middleStateTimer = 0, enteringFinishingStateTimer = 0, exitingFinishStateTimer = 0;
uint32_t startedEntering = 0,  startedExiting = 0;

struct identifyingAState
{
  int numberOfRisingEdge;
  int numberOfFallingEdge;
} currenttStateIdentifier;

int remeberingSumOfAllEdges;

void initDisplay()
{
	ssd1306_128x64_i2c_init();
	ssd1306_setFixedFont(ssd1306xled_font6x8);
  ssd1306_clearScreen();
  ssd1306_printFixedN(0, 24, "00 00", STYLE_NORMAL, 2);
}

#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_CUSTOM
void sntp_sync_time(struct timeval *tv)
{
   settimeofday(tv, NULL);
   ESP_LOGI(TAG, "Time is synchronized from custom code");
   sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}
#endif

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    sntp_init();
}

static void obtain_time(void)
{

    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 20;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
}

void showTime()
{
    time_t now;
    struct tm timeinfo;
    /*time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        obtain_time();
        // update 'now' variable with current time
        time(&now);
    }*/
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    else {
        // add 500 ms error to the current system time.
        // Only to demonstrate a work of adjusting method!
        {
            ESP_LOGI(TAG, "Add a error for test adjtime");
            struct timeval tv_now;
            gettimeofday(&tv_now, NULL);
            int64_t cpu_time = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
            int64_t error_time = cpu_time + 500 * 1000L;
            struct timeval tv_error = { .tv_sec = error_time / 1000000L, .tv_usec = error_time % 1000000L };
            settimeofday(&tv_error, NULL);
        }

        ESP_LOGI(TAG, "Time was set, now just adjusting it. Use SMOOTH SYNC method.");
        obtain_time();
        // update 'now' variable with current time
        time(&now);
    }
#endif

    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    char hour[16];
    char minute[16];
    char final[32] = "G2   ";

    time(&now);
    localtime_r(&now, &timeinfo);
    itoa(timeinfo.tm_hour, hour, 10);
    itoa(timeinfo.tm_min, minute, 10);

    if(timeinfo.tm_hour < 10){
        char tmp[16] = "0";
        strcat(tmp, hour);
        strcpy(hour, tmp);
    }

    if(timeinfo.tm_min < 10){
        char tmp[16] = "0";
        strcat(tmp, minute);
        strcpy(minute, tmp);
    }

    strcat(hour, ":");
    strcat(hour, minute);
    strcat(final, hour);
    
	  ssd1306_printFixedN(0, 0, final, STYLE_NORMAL, 1);
    // this will never happen anyways
    if (sntp_get_sync_mode() == SNTP_SYNC_MODE_SMOOTH) {
        struct timeval outdelta;
        while (sntp_get_sync_status() == SNTP_SYNC_STATUS_IN_PROGRESS) {
            adjtime(NULL, &outdelta);
            ESP_LOGI(TAG, "Waiting for adjusting time ... outdelta = %li sec: %li ms: %li us",
                        (long)outdelta.tv_sec,
                        outdelta.tv_usec/1000,
                        outdelta.tv_usec%1000);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
}

void publishEventClient(){
            
            time_t now;
            long long timestamp = time(&now);

            char payload[128];
            sprintf(
            payload, 
            "{\"username\":\"%s\",\"%s\":%d,\"device_id\":%d,\"timestamp\":%lld}",
            "group2_2021_ss",
            "sensor2",
            countOfPeopleInTheRoom,
            107,
            timestamp * 1000
            );

            ESP_LOGI(TAG, "%s", payload);
            int msg_id = esp_mqtt_client_publish(client_publisher, "50_107", payload, 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
}

void publishOneOnRestart(){
            
            time_t now;
            long long timestamp = time(&now);

            char payload[128];
            sprintf(
            payload, 
            "{\"username\":\"%s\",\"%s\":%d,\"device_id\":%d,\"timestamp\":%lld}",
            "group2_2021_ss",
            "restart",
            restartCount,
            107,
            timestamp * 1000
            );

            ESP_LOGI(TAG, "%s", payload);
            int msg_id = esp_mqtt_client_publish(client_publisher, "50_107", payload, 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
}

void publishEventClient_callback(void *param)
{
  publishEventClient();
}

static esp_err_t mqtt_event_handler_cb_subscriber(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_subscribe(client, "ROOM_EVENTS", 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            ESP_ERROR_CHECK(esp_timer_create(&publishClientTimer, &myTimerHandler3));
            ESP_ERROR_CHECK(esp_timer_start_periodic(myTimerHandler3, 15 * 60 * 1000 * 1000));

            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            esp_timer_stop(myTimerHandler3);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            char command[16];
            int i = 0;
            while (i < event->data_len) 
            {
              command[i] = event->data[i];
              i++;
            }
            command[i] = '\0';
            if(!strcmp(command, "enter")) {
              enterRoom();
            }
            else if(!strcmp(command, "leave")) {
              leaveRoom();
            }
            else if (!strcmp(command, "peak in"))
            {
              peakIntoRoom();
            }
            else if (!strcmp(command, "peak out"))
            {
              peakOutofRoom();
            }
            else if (!strcmp(command, "half way enter"))
            {
              halfWayEnter();
            }
            else if (!strcmp(command, "half way exit"))
            {
              halfWayExit();
            }
            else if (!strcmp(command, "entering change of mind"))
            {
              enteringChangeOfMindAtTheEnd();
            }
            else if (!strcmp(command, "exiting change of mind"))
            {
              exitingChangeOfMindAtTheEnd();
            }
            else if (!strcmp(command, "entering too fast"))
            {
              enteringTooFastForHuman();
            }
            else if (!strcmp(command, "exiting too fast"))
            {
              exitingTooFastForHuman();
            }
            else if (!strcmp(command, "obstruction inside"))
            {
              obstructionInside();
            }
            else if (!strcmp(command, "obstruction outside"))
            {
              obstructionOutside();
            }
            else if (!strcmp(command, "breakOuterAndInnerButReturnsG4"))
            {
              breaksOuterAndInnerButReturnsG4();
            }
            else if (!strcmp(command, "personTurnedG9"))
            {
              personTurnedG9();
            }
            else if (!strcmp(command, "unsureEnter"))
            {
              unsureEnter();
            }
            else if (!strcmp(command, "manipulationEnter"))
            {
              manipulationEnter();
            }
            else if (!strcmp(command, "peekIntoandLeaveG11"))
            {
              peekIntoandLeaveG11();
            }
            else if (!strcmp(command, "successiveEnter"))
            {
              successiveEnter();
            }
            else if(!strcmp(command, "ping")){
              ping();
            }
            vTaskDelay(1000/portTICK_PERIOD_MS);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static esp_err_t mqtt_event_handler_cb_publisher(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler_subscriber(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb_subscriber(event_data);
}

static void mqtt_event_handler_publisher(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb_publisher(event_data);
}

static void mqtt_app_start(void)
{

    esp_mqtt_client_config_t mqtt_cfg_subscriber = {
        .uri = "mqtt://user1:h8y98UyhX273X6tT@iotplatform.caps.in.tum.de:1885",
        .keepalive=120
    };
    client_subscriber = esp_mqtt_client_init(&mqtt_cfg_subscriber);
    esp_mqtt_client_register_event(client_subscriber, ESP_EVENT_ANY_ID, mqtt_event_handler_subscriber, client_subscriber);
    esp_mqtt_client_start(client_subscriber);

    esp_mqtt_client_config_t mqtt_cfg_publisher = {
        .uri = "mqtt://JWT:eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MjA4NzI2ODUsImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiNTBfMTA3In0.x40nxuvC7xAGbXsuM0HsxE_uzya3oGFfu-pcI09e0e4h_tIuDn6em0NxyAmDjbjECKpQPoCuTlam9BRV8VSZkv3YwYNNATxesQJQ0XYHA9QUTdXzSZgefiLTE00nSq9gPD6O2dddwx6egAr9xcPqlOC6WXOU9mb1pnFoOv0BMymLvsSJndsB4bxMxi3CYrMirFRxFzPaGFZvwRNMmZZ5oGWtLhtLJ7cJ0t0wdD9jV1334AQfCayQvb9n7_6E3ruSZjfUdGMj5jV9lFaChfKjzs2yq15dDfKbbfOkDGwLPrpURH3Y65ycuPyaUn6TTz_EwUF2d5Ai5i2x5cJn_TSQ2vbDJw3BZxcBZ7NxiXoG00OwQ02QXMAryyYJ1SKdr_BPxyy2C2XeZFGg3ip42bUkn92tScFiQtzUp-WaQSFWiZK3D1sdJglPb6l1iEQCI7HfNk0g0ADlvKh_aGTcxy4sjFM69phDfj_UV7T5Of-BV1lk7mifC2tdg1rqqttCMQ9uNbnsrw6YcfsO896zr7uFDaVsG98veUCXQYtuXkbn3XWqxGUM13H6nb2ThlZn2dMxi5EsWKXktyQDtAmB_Sf4akU6MU4uW-d8QpzTv9wPaW2Qo1KagA6KIcdM9AjT9J8BsLd8kzMyuUIpVoioT5REp3kIRACzcLLiY6YxyXCy22U@131.159.35.132:1883",
        .keepalive=120
    };
    client_publisher = esp_mqtt_client_init(&mqtt_cfg_publisher);
    esp_mqtt_client_register_event(client_publisher, ESP_EVENT_ANY_ID, mqtt_event_handler_publisher, client_publisher);
    esp_mqtt_client_start(client_publisher);

    publishOneOnRestart();

#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */
}

void showRoomState(enum  roomCounterChange changeInRoomCounter)
{
  //converting "int" to "string", so it could be written on the display
  uint8_t tmp = countOfPeopleInTheRoom;
  int count=0;
  while(tmp!=0)
  {
    tmp=tmp/10;
    count++;
  }

  //in case "count" variable is '0', so that we can make that number output on the display as well
  char myStr[count+1];
  tmp = countOfPeopleInTheRoom;
  for(int i = count-1; i>=0; i--)
  {
    myStr[i] = (tmp % 10) + '0';
    tmp = tmp / 10;
  }
  if(count != 0)
    myStr[count] = '\0';
  else
  {
    myStr[count] = '0';
    myStr[count+1] = '\0';
  }

  if(countOfPeopleInTheRoom < 10){
    char tmp[16] = "0";
    strcat(tmp, myStr);
    strcpy(myStr, tmp);
  }

	ssd1306_printFixedN(0, 24, myStr, STYLE_NORMAL, 2);
}

void writeOnDisplayTask(void* p)
{
  while(true)
  {
    volatile uint8_t theCount = 0;
    showTime();

    //if( xQueueReceive(displayQueueHandle, &theCount, portMAX_DELAY))
    if(xQueueReceive(displayQueueHandle, &theCount, ( TickType_t ) 100))
    {
      showRoomState(theCount);
    }  
  }
}

void changeAndDispalyNumberOfPeopleInTheRoom(enum roomCounterChange increaseOrDecreaseCounter)
{
  if(increaseOrDecreaseCounter == (enum roomCounterChange) increase)
  {
    countOfPeopleInTheRoom++;
    ESP_LOGI("ROOM COUNTER INCREASED", "The room counter is: %d\n", countOfPeopleInTheRoom );
  }
  else if(increaseOrDecreaseCounter == (enum roomCounterChange) decrease)
  {
    if(countOfPeopleInTheRoom > 0)
    {
      countOfPeopleInTheRoom--;
      ESP_LOGI("ROOM COUNTER DECREASED", "The room counter is: %d\n", countOfPeopleInTheRoom );
    }
    else
    {
      ESP_LOGE("MISTAKE WITH COUNTER", "The counter can't be decreased, because it is 0 (no people in the room).");
      increaseOrDecreaseCounter = (enum roomCounterChange) impossibleChange;
    }
  }

  // "portMAX_DELAY" means that we should be waiting (forever) until there is space on the queue which is free 
  if(!xQueueSend(displayQueueHandle, &increaseOrDecreaseCounter, portMAX_DELAY))
  {
    printf("Failed to send to display queue\n");
  }
}

static void IRAM_ATTR gpio_isr_handler(void *args)
{
  int pinNumber = (int)args;
  xQueueSendFromISR(interrputQueue, &pinNumber, NULL);
}

uint32_t timePassed(uint32_t timeInMilliseconds)
{
  uint32_t currentTime = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
  return  currentTime - timeInMilliseconds;
}

//CODE FOR TIMERS:
//This is timer for increasing the count of people in teh room
void timerIncreaseDecreaseCountOfPeopleInTheRoom_callback(void *param)
{
  //NOTE: "currentState" is a global variable
  
  uint32_t timePassedFromWhenEnteringOrExitingStarted = (int) param;

  if(currentState == (enum state) eneteringStateFinishingBothInactive)
  {
    if(timePassedFromWhenEnteringOrExitingStarted < FASTER_MOVEMENT_THAN_HUMAN)
    {
      printf("An object was detected entering, but counter not increased as it could not be a human being!\n");
    }
    else
    {
      printf("\nPerson stayed in 'eneteringStateFinishingBothInactive' state, so we increase the counter!\n");
      changeAndDispalyNumberOfPeopleInTheRoom((enum roomCounterChange) increase);
      publishEventClient();
    } 
  }
  else if(currentState == (enum state) exitingStateFinishingBothInactive)
  {
    if(timePassedFromWhenEnteringOrExitingStarted < FASTER_MOVEMENT_THAN_HUMAN)
    {
      printf("An object was detected entering, but counter not increased as it could not be a human being!\n");
    }
    else
    {
      printf("\nPerson stayed in 'exitingStateFinishingBothInactive' state, so we decrease the counter!\n");
      changeAndDispalyNumberOfPeopleInTheRoom((enum roomCounterChange) decrease);
      publishEventClient();
    }
  }
  /*else
  {
    printf("\nPerson changed position in less time than allowed for timer to increase/decrease!\n");
  }*/
}

void timerAvoidObstructionOfSensors_callback(void* param)
{
  int sumOfFallingAndRIsingEdgesOfSateInWhichTimerSTarted = (int) param;
  
  /*If we only used "numberOfRisingEdges", it would mean that the if statement would go through when a person 
  entered a room (or exit the room), as the number of rising edges would stay the same. The only way that the 
  number of rising edges would be different is if some other person started entering or exiting, which is not 
  what we should rely on, or even take as that in that case the program was working correctly beacuse it would
  only do what it should by chance.
  Also, as a struct value could not be sent to a timer, as a conversion between "void *" and some struct type is
  impossible in C unless alloaction in introduced, a trick is used. Each of the states that happen while the 
  machine is working is uniquely identified by the number of rising edges that happened before it, together with
  the number of rising edges that happened before it. What is characteristic for these two numbers is that they 
  must be positive and they are only increasing, so the sum of these two numbers also uniqely identifies each 
  state in the machine ends up. The summ of the two numbers is what is sent to this function.*/

  if(sumOfFallingAndRIsingEdgesOfSateInWhichTimerSTarted == 
        (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge) && 
        (currentState == (enum state) enteringOutsideActiveInsideInactive || currentState == (enum state) exitingOutsideInactiveInsideActive))
  {
    printf("Obstruction neglected, sumOfEdgesParam = %d, sum = %d ", sumOfFallingAndRIsingEdgesOfSateInWhichTimerSTarted, currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
    switch(currentState)
  {
    case 0:
      printf("currentState = startingState\n");
      break;
    case 1:
      printf("currentState = enteringOutsideActiveInsideInactive\n");
      break;

    case 2:
      printf("currentState = enetringBothActive\n");
      break;
    case 3:
      printf("currentState = enteringBothInactive\n");
      break;
    case 4:
      printf("currentState = enteringOutsideInactiveInsideActive\n");
      break;
    case 5:
      printf("currentState = eneteringStateFinishingBothInactive\n");
      break;

    case 6:
      printf("currentState = exitingOutsideInactiveInsideActive\n");
      break;
    case 7:
      printf("currentState = exitingBothActive\n");
      break;
    case 8:
      printf("currentState = exitingBothInactive\n");
      break;
    case 9:
      printf("currentState = exitingOutsideActiveInsideInactive\n");
      break;
    case 10:
      printf("currentState = exitingStateFinishingBothInactive\n");
      break;

    default:
      printf("impossible state");
  }
    currentState = (enum state) startState;
  }
  /*else{
    printf("\n\n, sumOfFallingAndRIsingEdgesOfSateInWhichTimerSTarted = %d, sum of edges = %d, current state = %d, currentStateID.numberIfRisingEdges = %d, currentStateID.numberIfFalingEdges = %d\n\n", 
    sumOfFallingAndRIsingEdgesOfSateInWhichTimerSTarted, 
    (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge), 
    currentState, currenttStateIdentifier.numberOfRisingEdge, currenttStateIdentifier.numberOfFallingEdge);
  }*/
}

//As this method is exclusively going to be called from the ISR, we are using function "xTaskGetTickCountFromISR()" in it
void changeState(enum action anAction)
{
  previousState = currentState;

  switch(currentState)
  {
    case 0:
      printf("BEGINNING: currentState = startingState\n");
      break;
    case 1:
      printf("BEGINNING: currentState = enteringOutsideActiveInsideInactive\n");
      break;

    case 2:
      printf("BEGINNING: currentState = enetringBothActive\n");
      break;
    case 3:
      printf("BEGINNING: currentState = enteringBothInactive\n");
      break;
    case 4:
      printf("BEGINNING: currentState = enteringOutsideInactiveInsideActive\n");
      break;
    case 5:
      printf("BEGINNING: currentState = eneteringStateFinishingBothInactive\n");
      break;

    case 6:
      printf("BEGINNING: currentState = exitingOutsideInactiveInsideActive\n");
      break;
    case 7:
      printf("BEGINNING: currentState = exitingBothActive\n");
      break;
    case 8:
      printf("BEGINNING: currentState = exitingBothInactive\n");
      break;
    case 9:
      printf("BEGINNING: currentState = exitingOutsideActiveInsideInactive\n");
      break;
    case 10:
      printf("BEGINNING: currentState = exitingStateFinishingBothInactive\n");
      break;

    default:
      printf("BEGINNING: impossible state");
  }


  if(currentState == (enum state) startState)
  {
    if(anAction == (enum action) risingEdgeOutside) //means that somebdy 'started' entering the room from outside
    {
      currentState = (enum state) enteringOutsideActiveInsideInactive;
      printf("END: currentState = enteringOutsideActiveInsideInactive\n");

      currenttStateIdentifier.numberOfRisingEdge++;
      if(previousState == (enum state) startState){
        remeberingSumOfAllEdges = (int) (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
        timerForAvoidingObstruction.arg = (void *) remeberingSumOfAllEdges;
      }
      ESP_ERROR_CHECK(esp_timer_create(&timerForAvoidingObstruction, &myTimerHandler2));
      /*The second argument of "esp_timer_start_once" function should be given in micro seconds, which is 
      why we multiply the "TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION" (which is given in milliseconds), which 
      represents time before something (i.e. a person or an object) is considered as an obstruction with 1000*/
      ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler2, TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION*1000));

      startedEntering = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    }
    else if(anAction == (enum action) risingEdgeInside) //means that somebody 'started' exiting from inside the room
    {
      currentState = (enum state) exitingOutsideInactiveInsideActive;
      printf("END: currentState = exitingOutsideInactiveInsideActive\n");

      currenttStateIdentifier.numberOfRisingEdge++;
      if(previousState == (enum state) startState){
        remeberingSumOfAllEdges = (int) (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
        timerForAvoidingObstruction.arg = (void *) remeberingSumOfAllEdges;
      }
      ESP_ERROR_CHECK(esp_timer_create(&timerForAvoidingObstruction, &myTimerHandler2));
      ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler2, TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION*1000));

      startedExiting = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    }
  }
  
  
  if(currentState == (enum state) enteringOutsideActiveInsideInactive) //intuitive
  {
    if(anAction == (enum action) fallingEdgeOutside)
    {
      currentState = (enum state) enteringBothInactive;
      /*The timer is set as based on it, we are going to conclude if a person continued with entering the room
      or just simply peaked inside and went away.
      */
      middleStateTimer = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
      
      printf("END: currentState = enteringBothInactive\n");
      
      currenttStateIdentifier.numberOfFallingEdge++;
    }
    else if(anAction == (enum action) risingEdgeInside)
    {
      currentState = (enum state) enetringBothActive;
      printf("END: currentState = enetringBothActive\n");

      currenttStateIdentifier.numberOfRisingEdge++;
      if(previousState == (enum state) startState){
        remeberingSumOfAllEdges = (int) (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
        timerForAvoidingObstruction.arg = (void *) remeberingSumOfAllEdges;
      }
      ESP_ERROR_CHECK(esp_timer_create(&timerForAvoidingObstruction, &myTimerHandler2));
      ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler2, TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION*1000));
    }
  }
  else if(currentState == (enum state) enetringBothActive) //A person standing horizontaly
  {
    if(anAction == (enum action) fallingEdgeOutside)
    {
      currentState = (enum state) enteringOutsideInactiveInsideActive;
      printf("END: currentState = enteringOutsideInactiveInsideActive\n");

      currenttStateIdentifier.numberOfFallingEdge++; 
    }
    else if(anAction == (enum action) fallingEdgeInside)
    {
      currentState = (enum state) enteringOutsideActiveInsideInactive;
      printf("END: currentState = enteringOutsideActiveInsideInactive\n");

      currenttStateIdentifier.numberOfFallingEdge++; 
    }
  }
  else if(currentState == (enum state) enteringBothInactive)  //may seem unintuitive
  {
    /*IMPORTANT: "enteringBothInactive" is different from "startingState", but if  "ALLOWED_TIME_IN_MIDDLE_STATE"
    passed in this state whitout experiencing any other action, than we conclude that the state in which the 
    machine is should not be "enteringBothInactive", but rather "startingSate". So, in a sense you could say that 
    "enteringBothInactive" is same as "startingState" after "ALLOWED_TIME_IN_MIDDLE_STATE" and no action in that 
    period.*/
    if(timePassed(middleStateTimer) < ALLOWED_TIME_IN_MIDDLE_STATE)
    {
      /*Based on the time that has passed from the moment we entered "enteringBothInactive" state we are 
      concluding if the person just peaked and turned around, or the person continued entering the room*/
      if(anAction == (enum action) risingEdgeOutside)
      {
        //Either a person is going back, or the first person was just peeking and this is another person
        currentState = (enum state) enteringOutsideActiveInsideInactive;
        printf("END: currentState = enteringOutsideActiveInsideInactive\n");

        currenttStateIdentifier.numberOfRisingEdge++;
        if(previousState == (enum state) startState){
        remeberingSumOfAllEdges = (int) (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
        timerForAvoidingObstruction.arg = (void *) remeberingSumOfAllEdges;
        }
        ESP_ERROR_CHECK(esp_timer_create(&timerForAvoidingObstruction, &myTimerHandler2));
        ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler2, TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION*1000));
      }
      else if(anAction == (enum action) risingEdgeInside)
      {
        currentState = (enum state) enteringOutsideInactiveInsideActive;
        printf("END: currentState = enteringOutsideInactiveInsideActive\n"); 

        currenttStateIdentifier.numberOfRisingEdge++;
        if(previousState == (enum state) startState){
        remeberingSumOfAllEdges = (int) (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
        timerForAvoidingObstruction.arg = (void *) remeberingSumOfAllEdges;
        }
        ESP_ERROR_CHECK(esp_timer_create(&timerForAvoidingObstruction, &myTimerHandler2));
        ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler2, TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION*1000));
      }
    }
    else if(timePassed(middleStateTimer) >= ALLOWED_TIME_IN_MIDDLE_STATE)
    {
      /*This is case when a person just peaked inside the room from the outside, and then left. So, if a person
      just peaked and left, that must mean that our machine had to be in "startState" instead of 
      "enteringBothInactive", which is what is done below. In other words, this means that the machine was in 
      wrong state before this new action happened, which is wha we need to call this function again, as only
      now it is in the correct state, and can answer to the new action correctly.*/
      currentState = (enum state) startState;
      printf("wrong state, change state\n");
      
      changeState(anAction);
    }
  }
  else if(currentState == (enum state) enteringOutsideInactiveInsideActive) //may seem unintuitive
  {
    if(anAction == (enum action) risingEdgeOutside) 
    {
      /*If we expereinece that the outer sensor is set to a logical 1, and after that experience a rising edge on 
      the sensor on the other side of the door (the inner sensor), this means that at the same time both sensors 
      are set to logical 1. A potential case in which this might happen is when a person who is entering the room 
      turnes horizontaly, breaking both sensors at the same time.*/
      currentState = (enum action) enetringBothActive;
      printf("END: currentState = enetringBothActive\n");

      currenttStateIdentifier.numberOfRisingEdge++;
      if(previousState == (enum state) startState){
        remeberingSumOfAllEdges = (int) (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
        timerForAvoidingObstruction.arg = (void *) remeberingSumOfAllEdges;
      }
      ESP_ERROR_CHECK(esp_timer_create(&timerForAvoidingObstruction, &myTimerHandler2));
      ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler2, TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION*1000));
    }
    else if(anAction == (enum action) fallingEdgeInside)
    {
      /* When the machine experiences a falling edge on the inside barrer, and before that it was in state
      "enteringOutsideInactiveInsideActive", this means that at this point we do not know if the person really
      finished with entering the room (in which case we need to increase the counter), or a person changed his
      mind and started going in the other way. This is why a timer is set, to see if we will experience any
      action in the next "ALLOWED_TIME_IN_MIDDLE_STATE", as this is the time in which we would need to experience
      an action if the person decided to turn around and go back from where he came. And in case we do not 
      experience any action in this period, we consider that a person entered the room.*/ 
      currentState = (enum state) eneteringStateFinishingBothInactive;
      enteringFinishingStateTimer = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;


      /*Set the timer to increase the count of people in the room if the person did not change his state 
      from "eneteringStateFinishingBothInactive" in "ALLOWED_TIME_IN_MIDDLE_STATE" milliseconds*/
      timerUntilEnteringOrExitingRoom.arg = (void *) timePassed(startedEntering);
      ESP_ERROR_CHECK(esp_timer_create(&timerUntilEnteringOrExitingRoom, &myTimerHandler1));
      /*The second argument of "esp_timer_start_once" function should be given in micro seconds, which is 
      why we multiply the "ALLOWED_TIME_IN_MIDDLE_STATE" (which is given in milliseconds) with 1000*/
      ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler1, ALLOWED_TIME_IN_MIDDLE_STATE*1000));

      middleStateTimer = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;

      printf("END: currentState = eneteringStateFinishingBothInactive\n");

      currenttStateIdentifier.numberOfFallingEdge++; 
    }
  } 
  else if(currentState == (enum state) eneteringStateFinishingBothInactive) //may seem unintuitive
  {
    printf("time passed = %d\n", timePassed(enteringFinishingStateTimer));
    /*When the machine is in "eneteringStateFinishingBothInactive" is in the "ALLOWED_TIME_IN_MIDDLE_STATE" we do
    not experience any actions, then it means that a person entered the room and that that the conunter of people
    in the room should be inceased. But, as this function is called only when the machine experences a new action,
    this would mean that we should not be in state "eneteringStateFinishingBothInactive" after 
    "ALLOWED_TIME_IN_MIDDLE_STATE", but rather in the "startingState". This means that the new action which came 
    inside this function should be handled by the machine which is in the "startingState", whichi is why in this 
    case we change the state, and call the function again, so that the action is handled properly.  
    
    On the other hand, if we do experience another action in (or less than) "ALLOWED_TIME_IN_MIDDLE_STATE", this 
    means that a person actualy never went completely inside of the room, but that rather he changed his mind and 
    went back, which is why the state of the machine should be changed from "eneteringStateFinishingBothInactive"
    into "enteringBothInactive".*/
    if(timePassed(enteringFinishingStateTimer) >= ALLOWED_TIME_IN_MIDDLE_STATE) 
    {
      currentState = (enum state) startState;
      printf("wrong state, change state(counter should have increased)\n");
      changeState(anAction);
    }
    else if(timePassed(enteringFinishingStateTimer) < ALLOWED_TIME_IN_MIDDLE_STATE)
    {
      currentState = (enum state) enteringBothInactive;
      printf("wrong state, change state(person not entering anymore)\n");
      changeState(anAction);
    }
  }

  //THIS PART OF THE CODE IS FOR EXITING OUT OF THE ROOM
  if(currentState == (enum state) exitingOutsideInactiveInsideActive)
  {
    if(anAction == (enum action) fallingEdgeInside)
    {
      currentState = exitingBothInactive;
      middleStateTimer = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
      printf("END: currentState = exitingBothInactive\n");

      currenttStateIdentifier.numberOfFallingEdge++; 
    }
    else if(anAction == (enum action) risingEdgeOutside)
    {
      currentState = exitingBothActive;
      printf("END: currentState = exitingBothActive\n");

      currenttStateIdentifier.numberOfRisingEdge++;
      if(previousState == (enum state) startState){
        remeberingSumOfAllEdges = (int) (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
        timerForAvoidingObstruction.arg = (void *) remeberingSumOfAllEdges;
      }
      ESP_ERROR_CHECK(esp_timer_create(&timerForAvoidingObstruction, &myTimerHandler2));
      ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler2, TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION*1000));
    }

  }
  else if(currentState == (enum state) exitingBothActive)
  {
    if(anAction == (enum action) fallingEdgeInside)
    {
      currentState = (enum state) exitingOutsideActiveInsideInactive;
      printf("END: currentState = exitingOutsideActiveInsideInactive\n");

      currenttStateIdentifier.numberOfFallingEdge++; 
    }
    else if(anAction == (enum action) fallingEdgeOutside)
    {
      currentState = (enum state) exitingOutsideInactiveInsideActive;
      printf("END: currentState = exitingOutsideInactiveInsideActive\n");

      currenttStateIdentifier.numberOfFallingEdge++; 
    }
  }
  else if(currentState == (enum state) exitingBothInactive)
  {
    if(timePassed(middleStateTimer) < ALLOWED_TIME_IN_MIDDLE_STATE)
    {
      if(anAction == (enum action) risingEdgeOutside)
      {
        currentState = (enum state) exitingOutsideActiveInsideInactive;
        printf("END: currentState = exitingOutsideActiveInsideInactive\n");

        currenttStateIdentifier.numberOfRisingEdge++;
      }
      else if(anAction == (enum action) risingEdgeInside)
      {
        currentState = (enum state) exitingOutsideInactiveInsideActive;
        printf("END: currentState = exitingOutsideInactiveInsideActive\n");

        currenttStateIdentifier.numberOfRisingEdge++;
        if(previousState == (enum state) startState){
        remeberingSumOfAllEdges = (int) (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
        timerForAvoidingObstruction.arg = (void *) remeberingSumOfAllEdges;
        }
        ESP_ERROR_CHECK(esp_timer_create(&timerForAvoidingObstruction, &myTimerHandler2));
        ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler2, TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION*1000));
      }
    }
    else if(timePassed(middleStateTimer) >= ALLOWED_TIME_IN_MIDDLE_STATE)
    {
      printf("wrong state, change state\n");
      currentState = (enum state) startState;
      changeState(anAction);
    }
  }
  else if(currentState == (enum state) exitingOutsideActiveInsideInactive)
  {
    if(anAction == (enum action) fallingEdgeOutside)
    {
      currentState = (enum state) exitingStateFinishingBothInactive;
      printf("END: currentState = exitingStateFinishingBothInactive\n");

      /*Set the timer to decrease the count of people in the room if the person did not change his state 
      from "exitingStateFinishingBothInactive" in "ALLOWED_TIME_IN_MIDDLE_STATE" milliseconds*/
      timerUntilEnteringOrExitingRoom.arg = (void *) timePassed(startedExiting);
      ESP_ERROR_CHECK(esp_timer_create(&timerUntilEnteringOrExitingRoom, &myTimerHandler1));
      /*The second argument of "esp_timer_start_once" function should be given in micro seconds, which is 
      why we multiply the "ALLOWED_TIME_IN_MIDDLE_STATE" (which is given in milliseconds) with 1000*/
      ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler1, ALLOWED_TIME_IN_MIDDLE_STATE*1000)); 

      exitingFinishStateTimer = xTaskGetTickCountFromISR()*portTICK_PERIOD_MS;
      middleStateTimer = xTaskGetTickCountFromISR()*portTICK_PERIOD_MS;

      currenttStateIdentifier.numberOfFallingEdge++; 
    }
    else if(anAction == (enum action) risingEdgeInside)
    {
      currentState = (enum state) exitingBothActive;
      printf("END: currentState = exitingBothActive\n");

      currenttStateIdentifier.numberOfRisingEdge++;
      if(previousState == (enum state) startState){
        remeberingSumOfAllEdges = (int) (currenttStateIdentifier.numberOfRisingEdge + currenttStateIdentifier.numberOfFallingEdge);
        timerForAvoidingObstruction.arg = (void *) remeberingSumOfAllEdges;
      }
      ESP_ERROR_CHECK(esp_timer_create(&timerForAvoidingObstruction, &myTimerHandler2));
      ESP_ERROR_CHECK(esp_timer_start_once(myTimerHandler2, TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION*1000));
    }
  }
  else if(currentState == (enum state) exitingStateFinishingBothInactive)
  {
    if(timePassed(exitingFinishStateTimer) < ALLOWED_TIME_IN_MIDDLE_STATE)
    {
      printf("wrong state, change state\n");
      currentState = (enum state) exitingBothInactive;
      changeState(anAction);
    }
    else if(timePassed(exitingFinishStateTimer) >= ALLOWED_TIME_IN_MIDDLE_STATE)
    {
      printf("wrong state, change state\n");
      currentState = (enum state) startState;

      changeState(anAction);
    }
  }
}

void doorBarrierTask(void *params)
{
  int pinNumber;
  int insideBarrierCount = 0, outsideBarrierCount = 0;

  while (true)
  {
    if (xQueueReceive(interrputQueue, &pinNumber, portMAX_DELAY))
    {
		  if(pinNumber==BUTTON_TRIGGERED_GPIO_OUTSIDE && 
          (xTaskGetTickCountFromISR()*portTICK_PERIOD_MS - lastTrigger_OutsideBarrierButton) > 8)
		  {
        lastTrigger_OutsideBarrierButton = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;

        previousState = currentState;

        if(gpio_get_level(BUTTON_TRIGGERED_GPIO_OUTSIDE) == 0)
        {
          printf("GPIO %d, outside barrier, falling edge\n", pinNumber); //from logical level 1, to logical level 0

          changeState((enum action) fallingEdgeOutside); 
        }
        else if(gpio_get_level(BUTTON_TRIGGERED_GPIO_OUTSIDE) == 1)
        {
          printf("GPIO %d, outside barrier, rising edge\n", pinNumber); //from logical level 0, to logical level 1

          changeState((enum action) risingEdgeOutside); 
        }
		  }
      else if(pinNumber==BUTTON_TRIGGERED_GPIO_INSIDE && 
          (xTaskGetTickCountFromISR()*portTICK_PERIOD_MS - lastTrigger_InsideBarrierButton) > 8)
      {
        lastTrigger_InsideBarrierButton = xTaskGetTickCountFromISR()*portTICK_PERIOD_MS;

        if(gpio_get_level(BUTTON_TRIGGERED_GPIO_INSIDE) == 0)
        {
          printf("GPIO %d, inside barrier, falling edge\n", pinNumber); //from logical level 1, to logical level 0

          changeState((enum action) fallingEdgeInside); 
        }
        else if(gpio_get_level(BUTTON_TRIGGERED_GPIO_INSIDE) == 1)
        {
          printf("GPIO %d, inside barrier, rising edge\n", pinNumber); //from logical level 0, to logical level 1

          changeState((enum action) risingEdgeInside); 
        }
      }
		  else if(pinNumber==BUTTON_TRIGGERED_GPIO_OUTSIDE)
		  {
			  printf("GPIO = %d, Avoiding bouncing problem. Now=%d,lastTrigger_OutsideBarrierButton=%d\n", BUTTON_TRIGGERED_GPIO_OUTSIDE,
            xTaskGetTickCountFromISR()*portTICK_PERIOD_MS, lastTrigger_OutsideBarrierButton);
		  }
      else
      {
        printf("GPIO = %d, Avoiding bouncing problem. Now=%d,lastTrigger_InsideBarrierButton=%d\n", BUTTON_TRIGGERED_GPIO_INSIDE,
            xTaskGetTickCountFromISR()*portTICK_PERIOD_MS, lastTrigger_InsideBarrierButton);
      }
    }
  }
}

void app_main(void)
{

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  ESP_ERROR_CHECK(example_connect());

  initDisplay();
  obtain_time();

  esp_reset_reason_t reason = esp_reset_reason();
    switch (reason)
    {
    case ESP_RST_POWERON:
        printf("first time\n");
        restartCount = 0;
        break;
    case ESP_RST_SW:
        printf("software reset\n");
        break;
    default:
        printf("unhandled event %d\n",reason);
        break;
    }
    restartCount++;
    printf("%d\n", restartCount);

  previousState = startState;
  currentState = startState;
  remeberingSumOfAllEdges = 0;

  currenttStateIdentifier.numberOfRisingEdge = 0;
  currenttStateIdentifier.numberOfFallingEdge = 0;

  lastTrigger_OutsideBarrierButton=0;
  lastTrigger_InsideBarrierButton=0;

	//esp_log_level_set("BLINK", ESP_LOG_ERROR);       
	esp_log_level_set("BUTTON", ESP_LOG_INFO);       

	//setting the GPIO input pin for the "outside" button/barrier
  gpio_pad_select_gpio(BUTTON_TRIGGERED_GPIO_OUTSIDE);
  gpio_set_direction(BUTTON_TRIGGERED_GPIO_OUTSIDE, GPIO_MODE_INPUT_OUTPUT);
  gpio_pulldown_en(BUTTON_TRIGGERED_GPIO_OUTSIDE);
  gpio_pullup_dis(BUTTON_TRIGGERED_GPIO_OUTSIDE);
  gpio_set_intr_type(BUTTON_TRIGGERED_GPIO_OUTSIDE, GPIO_INTR_ANYEDGE); //rising ednge (i.e. from 0 to 1)

  //setting the GPIO input pin for the "inside" button/barrier
  gpio_pad_select_gpio(BUTTON_TRIGGERED_GPIO_INSIDE);
  gpio_set_direction(BUTTON_TRIGGERED_GPIO_INSIDE, GPIO_MODE_INPUT_OUTPUT);
  gpio_pulldown_en(BUTTON_TRIGGERED_GPIO_INSIDE);
  gpio_pullup_dis(BUTTON_TRIGGERED_GPIO_INSIDE);
  gpio_set_intr_type(BUTTON_TRIGGERED_GPIO_INSIDE, GPIO_INTR_ANYEDGE); //rising edge (i.e. from 0 to 1)

  interrputQueue = xQueueCreate(25, sizeof(int));

	TaskHandle_t Task1;
  xTaskCreate(doorBarrierTask, "taskForInerrupt", 8000, NULL, tskIDLE_PRIORITY, &Task1);

	gpio_install_isr_service(0);

  gpio_isr_handler_add(BUTTON_TRIGGERED_GPIO_OUTSIDE, gpio_isr_handler, (void *)BUTTON_TRIGGERED_GPIO_OUTSIDE);
  gpio_isr_handler_add(BUTTON_TRIGGERED_GPIO_INSIDE, gpio_isr_handler, (void *)BUTTON_TRIGGERED_GPIO_INSIDE);

  //task for the display:
  TaskHandle_t Task2;
  xTaskCreate(writeOnDisplayTask, "taskForDisplay", 8000, NULL, tskIDLE_PRIORITY, &Task2);
  displayQueueHandle = xQueueCreate(5, sizeof(enum  roomCounterChange));

  ESP_LOGI(TAG, "[APP] Startup..");
  ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
  ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
  esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
  esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
  esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
  esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
  esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

  mqtt_app_start();

  printf("espressed in milliseconds, ALLOW_TIME_IN_MIDDLE_STATE=%d\n", ALLOWED_TIME_IN_MIDDLE_STATE);
	while(true) 
	{
    //read what functions to use instead of "esp_log_timestamp()": https://www.esp32.com/viewtopic.php?t=11684
    vTaskDelay(10000/portTICK_PERIOD_MS);
	}
}

