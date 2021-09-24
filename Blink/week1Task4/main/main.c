#include <stdio.h>

#include "esp_system.h"
#include "math.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h" //!!!!!!!!!!!!!!!!!!!!!!! it can't be just '#include queue.h', because it output an error

#include "driver/gpio.h"

//for using the function for converting string to double
#include <stdlib.h>

#include "esp_log.h"

/*
This program was tested using GPIO pin 26, 18, 19, 23, 5, 33, 22, 21, 27, 25, 32, 4, 0, 2.

EXAMPLE:
The values you migfht want to input with your keyboard to see that the program workes:
1. For the GPIO enter any of GPIO-s from set {26, 18, 19, 23, 5, 33, 22, 21, 27, 25, 32, 4, 0, 2}
2. For the time that led is on insert 3000 (which refers to milliseconds).
3. For the frequency enter 0.1 (which is expressed in Hz). => 7000 miliseconds the led is turned of 
*/

#define ONE_Hz_TO_ms 1000

void inputFunction(char anArray[], int sizeOfArray) 
{
  char character = 'a';

  int i=0;
  while (i<sizeOfArray-1 && character!='\n') 
  {
    character=getchar();

    if(character!=0xff && character!='\n')
    {
      anArray[i]=character;
      i++;
      printf("%c", character);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
  
  anArray[i]='\0';
}
void readOutAStringCharByChar(char* anArray, int sizeofAnArray)
{
  printf("\n");
  for(int i=0; i<sizeofAnArray; i++)
  {
    printf("%c", *(anArray+i));

    if(*(anArray+i)=='\0')
    {
      break;
    }
  }
}

int stringToInt(char *myStr, int strLength)
{
  int number = 0;

  for(int i=0; i<strLength; i++)
  {
    //printf("*(myStr+i)==%c\n", *(myStr+i));
    if(*(myStr+i)=='\0')
      break;

    int firstDigit = *(myStr+i) - '0';
    number = number*10 + firstDigit;
  }

  //printf("value being returned from 'stringToInt' function: %d\n", number);
  return number;
}

bool isStringANumber(char *myString, int strLength)
{
  bool pointRead=false;
  int count=0;
  for(int i=0; i<strLength; i++)
  {
    if(i==0 && (myString[i]==' ' || myString[i]=='\n' || myString[i]=='\t'))
    {
      while(i<strLength && (myString[i]==' ' || myString[i]=='\n' || myString[i]=='\t'))
        i++;
    }

    //printf("current character is: '%c'\n", myString[i]);
    if(myString[i]=='.' && pointRead==false)
    {
      pointRead=true;
    }
    else if(myString[i]=='\0')
      break;
    else if((myString[i]-'0')<0 || (myString[i]-'0')>9)
    {
      printf("Input: ");
      readOutAStringCharByChar(myString, strLength);
      printf(" can not be read as double, please try again!\n");
      return false;
    }
    count++;  
  }

  if(count==0)
    return false;

  return true;
}

bool isStringAGPIOpinCode(char* myStr, int strLength)
{
  char GPIOpins[22][4] = {
                          //{'V', 'P', '\0', '\0'},
                          {'2', '6', '\0', '\0'},
                          {'1', '8', '\0', '\0'},
                          {'1', '9', '\0', '\0'},
                          {'2', '3', '\0', '\0'},
                          {'5', '\0', '\0', '\0'},
                          //{'V', 'N', '\0', '\0'},
                          //{'3', '5', '\0', '\0'},
                          {'3', '3', '\0', '\0'},
                          //{'3', '4', '\0', '\0'},
                          //{'T', 'R', '0', '\0'},
                          //{'R', 'X', '0', '\0'},
                          {'2', '2', '\0', '\0'},
                          {'2', '1', '\0', '\0'},
                          //{'1', '7', '\0', '\0'},
                          //{'1', '6', '\0', '\0'},
                          {'2', '7', '\0', '\0'},
                          {'2', '5', '\0', '\0'},
                          {'3', '2', '\0', '\0'},
                          {'4', '\0', '\0', '\0'},
                          {'0', '\0', '\0', '\0'},
                          {'2', '\0', '\0', '\0'},
                        };

  /*char GPIOpins[22][4] = {
                          "VP",
                          "26",
                          "18",
                          "19",
                          "23",
                          "5",
                          "VN",
                          "35",
                          "33",
                          "34",
                          "TR0",
                          "RX0",
                          "22",
                          "21",
                          "17",
                          "16",
                          "27",
                          "25",
                          "32",
                          "4",
                          "0",
                          "2",
                        };*/

  bool sameStrings = true;
  for(int i=0; i<22; i++)
  {
    char* s1 = GPIOpins[i];
    char* s2 = myStr;

    sameStrings = true;
    /*printf("Comparing strings: ");
    readOutAStringCharByChar(s1, 100);
    printf(" and ");
    readOutAStringCharByChar(s2, 100);*/

    while (*s1!='\0' && *s2!='\0') 
    {
      if(*s1!=*s2)
      {
        sameStrings=false;
        //printf("sameString==%d, s1==%c, s2==%c\n", sameStrings, *s1, *s2);
        break;
      }
      else
      {
        s2++;
        s1++; 
      }
    }

    /*printf("After comparisson: sameString==%d, s1==%c, s2==%c\n", sameStrings, *s1, *s2);
    if(*s1=='\0')
    {
      printf("*s1 is equal to '\\0'");
    }

    if(*s2=='\0')
    {
      printf("*s2 is equal to '\\0'");
    }
    else
    {
      printf("*s2 is equal to %d", *s2);
    }*/

    if(sameStrings==true && *s1=='\0' && *s2=='\0')
    {
      printf("\nString: ");
      readOutAStringCharByChar(myStr, 100);
      printf(" is a GPIO pin code\n");
      return true;
    }
    /*printf("Function strcmp returned: %d\n", strcmp(s1, s2));

    if(strcmp(s1, s2)==0)
    {
      printf("String: ");
      readOutAStringCharByChar(myStr, 100);
      printf("is a GPIO pin code\n");
      return true;
    }*/
  }

  printf("\nString: ");
  readOutAStringCharByChar(myStr, 100);
  printf(" is not a GPIO pin code, try again!\n");

  return false;
}

struct turnOnLEDinfo
{
  int intMyStringPin1;

  double doubleFrequency;

  int counterOfFlashes;

  double periodTurnedOn;
  double periodTurnedOff;
} led;

xQueueHandle globalQueueHandle; 

void blinkingTaskSender(void* p)
{
  led.counterOfFlashes = 1;
  //reading in the input variable
  int count = (int*) p;

  gpio_pad_select_gpio(led.intMyStringPin1);
  gpio_set_direction(led.intMyStringPin1, GPIO_MODE_OUTPUT);
  printf("LED lamp is turend off\n"); 

  while(true) 
	{
    //printf("The count is: %d", count);
    //printf("The count is: %d\n", led.counterOfFlashes);
    gpio_set_level(led.intMyStringPin1, 0);
	  printf("The led is OFF for: %f (the %d time, variable 'counter')\n", led.periodTurnedOff, count);
    printf("SENDER TASK: The GPIO pin is (function gpio_get_level): %d\n", gpio_get_level(led.intMyStringPin1));
    vTaskDelay(led.periodTurnedOff/portTICK_PERIOD_MS);

    // "portMAX_DELAY" means that we should be waiting (forever) until there is space on the queue which is free 
    if(!xQueueSend(globalQueueHandle, &led, portMAX_DELAY))
    {
      printf("Failed to send to queue");
    }

    vTaskDelay(led.periodTurnedOn/portTICK_PERIOD_MS);

    led.counterOfFlashes++;
    count++;
	}
}

void blinkingTaskReciever(void* p)
{
  while(true)
  {
    if( xQueueReceive(globalQueueHandle, &led, portMAX_DELAY))
    {
		  gpio_set_level(led.intMyStringPin1, 1);
      printf("The led is ON for: %f (the %d time, 'led.counterOfFlashes')\n", led.periodTurnedOn, led.counterOfFlashes);

      printf("RECIEVER TASK: The GPIO pin is (function gpio_get_level): %d\n", gpio_get_level(led.intMyStringPin1));

		  vTaskDelay(led.periodTurnedOn/portTICK_PERIOD_MS);
    }
    else
    {
      printf("Failed to recieve anything");
    } 
  }
}

void app_main(void)
{
  printf("Please enter a number representing one of a GPIO pin code.\n");
  
  char myStringPin1[100];
  
  do
  {
    inputFunction(myStringPin1, 100);
    printf("\n");
  } while (isStringAGPIOpinCode(myStringPin1, 100)==false);
  ESP_LOGI("SUCCESS", "The GPIO pin was successfully choosed as pin: %s", myStringPin1);

  printf("\nPlease enter the number of miliseconds you want the LED to be turend on: ");
  char milisecondLEDon[100];
  do
  {
    inputFunction(milisecondLEDon, 100);
    printf("\n");
  } while(isStringANumber(milisecondLEDon, 100)==false);
  char *ptr;
  double numberOfMilisecondsLEDon = strtod(milisecondLEDon, &ptr);
  ESP_LOGI("SUCCESS", "Time the LED is going to be ON successfully set to: %f ms", numberOfMilisecondsLEDon);

  printf("\nPlease enter the frequency (in Hz, i.e. 1/s) you want the LED to be turend of: ");
  char frequency[100];
  double doubleFrequency;
  do
  {
    do
    {
      inputFunction(frequency, 100);
      printf("\n");
    } while(isStringANumber(frequency, 100)==false);
    char *ptr2;
    doubleFrequency = strtod(frequency, &ptr2);

    if(doubleFrequency < 0.0001 || (doubleFrequency > 0.0001  && ((1/doubleFrequency)*1000 - numberOfMilisecondsLEDon)<0.0001) )
      ESP_LOGW("WRONG FREQUENCY", "The frequency you entered is a number, but according to the tim the LED is OFF, such frequency can't exist.\n Please try again!");

    printf("\n-------\n");
    printf("doubleFrequency = %f\n", doubleFrequency);
    printf("doubleFrequency < 0.00001 = %d\n", doubleFrequency<0.00001);
    printf("numberOfMilisecondsLEDon = %f\n", numberOfMilisecondsLEDon);
    printf("(1/doubleFrequency)*1000 - numberOfMilisecondsLEDon = %f\n", (1/doubleFrequency)*1000 - numberOfMilisecondsLEDon);
    printf("myStringPIn1 = %s\n", myStringPin1);
    printf("-------\n");

  } while(doubleFrequency < 0.0001 || (doubleFrequency > 0.0001  && ( (1/doubleFrequency)*1000 - numberOfMilisecondsLEDon)<0.0001) );
  ESP_LOGI("SUCCESS", "The frequency specified correctly as: %f Hz", doubleFrequency);

  int intMyStringPin1 = stringToInt(myStringPin1, 100);

  double dPeriodTurnedOff =  (1/doubleFrequency)*ONE_Hz_TO_ms - numberOfMilisecondsLEDon;
    //As 1[Hz]=1[1/s], to convert HZ to ms, we would have that 1[Hz]=1[1/1000ms]  

  led.intMyStringPin1 = intMyStringPin1;
  led.periodTurnedOn = numberOfMilisecondsLEDon;
  led.periodTurnedOff = dPeriodTurnedOff;
  led.doubleFrequency = doubleFrequency;

  printf("\n-------\n");
  printf("(1/doubleFrequency)*1000 - numberOfMilisecondsLEDon = %f\n", (1/doubleFrequency)*1000 - numberOfMilisecondsLEDon);
  printf("doubleFrequency > 0.00001 = %d\n", doubleFrequency>0.00001);
  ESP_LOGI("CHOOSEN PIN", "The GPIO pin you choose is: led.intMyStringPin1 = %d", led.intMyStringPin1);
  printf("led.periodTurnedOn = %f\n", led.periodTurnedOn);
  ESP_LOGI("CHOOSEN DURATION THAT LED IS ON", "led.periodTurnedOn = %f", numberOfMilisecondsLEDon);
  printf("led.periodTurnedOff = %f\n", led.periodTurnedOff);
  ESP_LOGI("CHOOSEN FREQUENCY", "led.doubleFrequency = %f", led.doubleFrequency);
  printf("-------\n");


  /*
 BaseType_t xTaskCreate(    TaskFunction_t pvTaskCode,
                            const char * const pcName,
                            configSTACK_DEPTH_TYPE usStackDepth, 
                            //1240 is very low, you should try bigger numbers, as if you don't do so, you might experience a stack overflow
                            void *pvParameters,
                            UBaseType_t uxPriority,
                            TaskHandle_t *pxCreatedTask
                          );
*/

  //first parameter specifies how many elements can the queue have, while the second specifies the size of each element
  globalQueueHandle = xQueueCreate(2, sizeof(struct turnOnLEDinfo));

  TaskHandle_t TaskH1, TaskH2;

  int count = 1;
  //"tskIDLE_PRIORITY" is default value for priority number 0, nad to increase we just add an some int to this number
  //you tried to pass a struct in a task instead of an int, but according to documentation you should not do that, and it didn't also work
  xTaskCreate(blinkingTaskSender, "task1", 8000, (void *) count, tskIDLE_PRIORITY, &TaskH1);
  xTaskCreate(blinkingTaskReciever, "task2", 8000, (void *) count, tskIDLE_PRIORITY, &TaskH2);
}
