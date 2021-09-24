#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "commands.h"

#define triggerPinOut 33
#define triggerPinIn 4


/*
"ALLOWED_TIME_IN_MIDDLE_STATE" is a variable used to represent the time it takes for the other sensor (with 
word "other" it is referred to whichever sensor is on the other side of the door a person is currently at, 
i.e. if a person is entering, "other" refers to the inner sensor/barrier, and if a person is exiting the room, 
"other" refers to the outer sensor/barrier) to register that the someone (a human being) has interrupted the 
laser that the sensor uses to detect movement, when the first sensor goes off. In other words,taking the 
example when a person enters the room, the person passing through the door must interrupt first the outer sensor, and then 
the outer sensor goes off, but the inner sensor has to detect movement (i.e. go on) in the number of 
milliseconds specified in the variable "ALLOWED_TIME_IN_MIDDLE_STATE", in order for the actions not to be 
characterized as just peaking from the outside of the room. The time in which the second sensor (in case of 
entering the room the "second laser" is the inner laser/barrier) must go on (i.e. detect breaking of the 
sensor laser light), after the first one (in the case of entering the "first laser" is the outer laser/barrier)
goes off, is a very short time that takes the sensor to register that the laser interruption has occurred. 
The same logic is applied when a person is exiting the room, as the explained for entering the room. If the
laser is cut off from the other side after more than the time defined in variable 
"ALLOWED_TIME_IN_MIDDLE_STATE", that action is not considered to refer to the same person (or at least not 
to his/hers first try of entering or exiting), because if the action took more than 
"ALLOWED_TIME_IN_MIDDLE_STATE", it means that a person just peeked from one side of the room. So, whichever 
action happens after "ALLOWED_TIME_IN_MIDDLE_STATE" is considered to be an action of another person (or 
another try of entering/exiting of the same person). 
In addition, the same variable "ALLOWED_TIME_IN_MIDDLE_STATE" if the time that is needed for a sensor to 
register that it's laser is no longer broken, i.e. that it can go from on to off, which is why we need to wait
this much time when a person is entering/exiting and see if the variable "currentState" will not change in 
the meanwhile, in order to count that a person entered/exited the room, and not turned around (like changing
his/hers mind and not completing the action).
*/
#define ALLOWED_TIME_IN_MIDDLE_STATE 115
#define TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION 9000
#define FASTER_MOVEMENT_THAN_HUMAN 80

static const char *TAG = "commands";

//This function should decrease the counter, but only if the counter is greater than zero. If the counter is zero, it will not decrease, and an
// error message is going to be output in the console. 

void ping()
{
	gpio_set_direction(19, GPIO_MODE_OUTPUT);
    gpio_set_level(19, 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    gpio_set_level(19, 0);
}

void leaveRoom()
{
	ESP_LOGI(TAG,"Command: Leave");

	gpio_set_level(triggerPinIn,1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinIn,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE - 15) / portTICK_RATE_MS);
	
	gpio_set_level(triggerPinOut,1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinOut,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 15) / portTICK_RATE_MS); //additional 20 millisec to make sure that enough time has passed
}

//This function should increase the counter.
void enterRoom()
{
	ESP_LOGI(TAG,"Command: Enter");

	gpio_set_level(triggerPinOut,1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinOut,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE - 15) / portTICK_RATE_MS);
	
	gpio_set_level(triggerPinIn,1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinIn,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 15) / portTICK_RATE_MS);
}

//This function should not increase the counter.
void peakIntoRoom()
{
	//The sequence of states that the machine will be in is: "enteringOuterActiveInnerInactive", "enteringBothInactive", "startingState".
    ESP_LOGI(TAG,"Command: Peak In");

    gpio_set_level(triggerPinOut, 1);
    vTaskDelay(50 / portTICK_RATE_MS);
    gpio_set_level(triggerPinOut, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 15) / portTICK_RATE_MS); //the variable "currentState" will be set to "startState"
}

//This function should not increase the counter.
void peakOutofRoom()
{
	//The sequence of states that the machine will be in is: "exitingOuterInactiveInnerActive", "exitingBothInactive", "startingState"
    ESP_LOGI(TAG,"Command: Peak Out");

    gpio_set_level(triggerPinIn, 1);
    vTaskDelay(50 / portTICK_RATE_MS);
    gpio_set_level(triggerPinIn, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 15) / portTICK_RATE_MS);
}

//This function should not increase the counter.
void halfWayExit()
{
/*someone go to the middle of the doorway, and then turns around*/

//This situation is equvalent to somebody breaking the inner sensor, and then turning horizontally, and then going back inside of the room.
//The sequence of states that the machine will be in is: "exitingOuterInactiveInnerActive", "exitingBothActive", 
// "exitingOuterInactiveInnerActive", "startingState".
    ESP_LOGI(TAG,"Command: Half Exit");

    gpio_set_level(triggerPinIn, 1);
    vTaskDelay(50 / portTICK_RATE_MS);
    gpio_set_level(triggerPinOut, 1);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE - 15) / portTICK_RATE_MS);

	gpio_set_level(triggerPinOut, 0);
    vTaskDelay(50 / portTICK_RATE_MS);
    gpio_set_level(triggerPinIn, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 15) / portTICK_RATE_MS);
}

//This function should not increase the counter.
void halfWayEnter()
{
//This situation is equvalent to somebody breaking the outer sensor, and then turning horizontally, and then going back outside of the room.
//The sequence of states that the machine will be in is: "enteringOuterActiveInnerInactive", "enteringBothActive", 
// "enteringOuterActiveInnerinactive", "startingState".
    ESP_LOGI(TAG,"Command: Half Enter");

    gpio_set_level(triggerPinOut, 1);
    vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinIn, 1);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE - 15) / portTICK_RATE_MS);

    gpio_set_level(triggerPinIn, 0);
    vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinOut, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 15) / portTICK_RATE_MS);
}

//This function should not increase the counter.
void enteringChangeOfMindAtTheEnd()
{
	/* 
	This is a situation where a person is exiting the room, and where the same sequence  of actions happens as when a person is exiting the room 
	and is counted as a person who exited the room (the counter decreases), but in this case the person is not counted as if he/she exited the room 
	because of the time constraints that we set. Firstly the inner barrier goes on, then the inner barrier goes off, then in a short space of time 
	(to be more precise in less then "ALLOWED_TIME_IN_MIDDLE_STATE") the outer barrier goes on, and then the outer barrier goes off. 
	There are two possible outcomes when we come to such situation, and the outcome depends on time. 
	If we experience any action in less than "ALLOWED_TIME_IN_MIDDLE_STATE" milliseconds it would mean that a person changed his/hers mind at the last moment 
	before exiting and turned around (this is the case tested in this function), and in this case the counter would not be decreased. On the other hand, 
	if there was no action in the "ALLOWED_TIME_IN_MIDDLE_STATE" milliseconds after the outer barrier went off, we would count that a person exited the room 
	(this case is tested in the "exitRoom" function).
 	*/

	ESP_LOGI(TAG,"Command: Change of mind at the end when entering.");

	gpio_set_level(triggerPinOut,1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinOut,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE - 25) / portTICK_RATE_MS);
	
	gpio_set_level(triggerPinIn,1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinIn,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE - 25) / portTICK_RATE_MS); 
	
	gpio_set_level(triggerPinOut,1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinOut, 0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 25) / portTICK_RATE_MS); 
	/*
	Note that the only difference between this function and the function for entering the room is the fact that insead of adding 20 milli second
	we are subtracting 20 milli seconds, making sure that the delay is shorter than the "ALLOWED_TIME_IN_MIDDLE_STATE", which is the least time 
	that we have to wait until a new action in the system, in order for the counter to increase. Also, we trigger the inner barrier again 
	then (triggered in less than "ALLOWED_TIME_IN_MIDDLE_STATE" from when it was set to logical level 0), making the person enetering the room
	not count, as this person emediately turened around. 
	*/
}

//This function should not increase the counter.
void exitingChangeOfMindAtTheEnd()
{
	/* 
	This is a situation where a person is exiting the room, and where the same sequence  of actions happens as when a person is exiting the room 
	and is counted as a person who exited the room (the counter decreases), but in this case the person is not counted as if he/she exited the room 
	because of the time constraints that we set. Firstly the inner barrier goes on, then the inner barrier goes off, then in a short space of time 
	(to be more precise in less then "ALLOWED_TIME_IN_MIDDLE_STATE") the outer barrier goes on, and then the outer barrier goes off. 
	There are two possible outcomes when we come to such situation, and the outcome depends on time. 
	If we experience any action in less than "ALLOWED_TIME_IN_MIDDLE_STATE" milliseconds it would mean that a person changed his/hers mind at the last moment 
	before exiting and turned around (this is the case tested in this function), and in this case the counter would not be decreased. 
	On the other hand, if there was no action in the "ALLOWED_TIME_IN_MIDDLE_STATE" milliseconds after the outer barrier went off, 
	we would count that a person exited the room (this case is tested in the "exitRoom" function).
 	*/

	ESP_LOGI(TAG,"Command: Change of mind at the end when exiting.");

	gpio_set_level(triggerPinIn,1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinIn,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE - 25) / portTICK_RATE_MS);

	gpio_set_level(triggerPinOut,1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinOut,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE - 25) / portTICK_RATE_MS); 
	
	gpio_set_level(triggerPinIn, 1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinIn, 0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 25) / portTICK_RATE_MS); 
	/*
	Note that the only difference between this function and the function for exiting the room is the fact that insead of adding 20 milli second
	we are subtracting 20 milli seconds, making sure that the delay is shorter than the "ALLOWED_TIME_IN_MIDDLE_STATE", which is the least time 
	that we have to wait until a new action in the system, in order for the counter to decrease. Also, we trigger the outer barrier again 
	then (triggered in less than "ALLOWED_TIME_IN_MIDDLE_STATE" from when it was set to logical level 0), making the person exiting the room
	not count, as this person emediately turened around. 
	*/
}

void enteringTooFastForHuman()
{
	/*
	You can imagine this example as if an object which is faster than the fastest human on earth entered the 
	room, like for an example a bullet.
	*/
	ESP_LOGI(TAG,"Command: Entering too fast");
	
	gpio_set_level(triggerPinOut, 1);
	vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_RATE_MS);
	gpio_set_level(triggerPinOut, 0);
	vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_RATE_MS);
	
	gpio_set_level(triggerPinIn, 1);
	vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_RATE_MS);
	gpio_set_level(triggerPinIn, 0);
	vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_RATE_MS);
}

void exitingTooFastForHuman()
{
	ESP_LOGI(TAG,"Command: Exiting too fast");

	gpio_set_level(triggerPinIn, 1);
	vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_RATE_MS);
	gpio_set_level(triggerPinIn, 0);
	vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_RATE_MS);
	
	gpio_set_level(triggerPinOut, 1);
	vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_RATE_MS);
	gpio_set_level(triggerPinOut, 0);
	vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_RATE_MS);
}

//This function should not increase the counter.
void obstructionInside()
{
	/*Someone or something is standing in the inside barrier, making counting impossible. So, if such an object
	stands there for more than it is a constant "TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION", we consider as if it
	is a obrstuction for our counter, and we don't condiser it, asn set the state of the machine to 
	"startingSate". An example of what might happen is if somebody put a backback on one side of the door, 
	and the sensor detects it, but neglectes it after "TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION" of standing and 
	not moving. On the other hand, if the reason why the barrier is blocked is not an object, but rather a 
	person, after the machine is set to be in state "startingState", it could only stay in this state if the 
	person was not moving. So, if the person who was standing on teh door at one point choose to go inside, 
	after a slight movement the sensor would again go on. This situation is hard to test with buttons, and 
	it would be much easier with real sensors, as when you press a button, hold it, and then the code sets
	the level after "TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION", the only thing you can do is let go of the button,
	but in the sensors case it would be easier in asense that we would not have to have the falling edge as we 
	do wehn we lwt go of the button.*/
	ESP_LOGI(TAG,"Command: Obstruction Inside");

	gpio_set_level(triggerPinIn, 1);
	vTaskDelay( (TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION+50) / portTICK_RATE_MS);

	gpio_set_level(triggerPinIn, 0);
	vTaskDelay(50 / portTICK_RATE_MS);

	gpio_set_level(triggerPinOut, 1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinOut, 0);
	vTaskDelay( (ALLOWED_TIME_IN_MIDDLE_STATE+50) / portTICK_RATE_MS);
}

//This function should not increase the counter.
void obstructionOutside()
{
	ESP_LOGI(TAG,"Command: Obstruction Outside");

	gpio_set_level(triggerPinOut, 1);
	vTaskDelay( (TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION+50) / portTICK_RATE_MS);

	gpio_set_level(triggerPinOut, 0);
	vTaskDelay(50 / portTICK_RATE_MS);

	gpio_set_level(triggerPinIn, 1);
	vTaskDelay(50 / portTICK_RATE_MS);
	gpio_set_level(triggerPinIn, 0);
	vTaskDelay( (ALLOWED_TIME_IN_MIDDLE_STATE+50) / portTICK_RATE_MS);
}

/**
 * Corner case from Group 4: Almost Enter (big person). 
 * When the student decides to turn around the student has already
 * unblocked the outer barrier but not the inner one.
 * expected outcome: no change
 */

void breaksOuterAndInnerButReturnsG4()
{
    ESP_LOGI(TAG, "Command: breakOuterAndInnerButReturnsG4");
    gpio_set_level(triggerPinOut, 1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinIn, 1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinOut, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);

    gpio_set_level(triggerPinOut, 1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinIn, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinOut, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 20) / portTICK_PERIOD_MS);
}

/** 
 * Corner case from Group9: Almost Enter (slim person).
 * a person enters the room (breaking the first and then the second light barrier),
 * turns around (while the second light barrier is still broken),
 * and leaves the rooms (breaking the first light barrier again quickly after).
 * expected outcome: no change
 */

void personTurnedG9() {
    ESP_LOGI(TAG,"Command: Person entered the room and turned around");
    // person entering
    gpio_set_level(triggerPinOut, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinOut, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE-20) / portTICK_PERIOD_MS);
    // person turning around
    gpio_set_level(triggerPinIn, 1);
    vTaskDelay(300 / portTICK_PERIOD_MS); // turning takes time
    gpio_set_level(triggerPinIn, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE-20) / portTICK_PERIOD_MS);
    // person left the room again
    gpio_set_level(triggerPinOut, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinOut, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE+20) / portTICK_PERIOD_MS);
}

/** 
 * someone almost enters the room, but takes one step back (PinIn goes low before PinOut)
 * before finally entering.
 * expected outcome: +1
 */

void unsureEnter() {
    ESP_LOGI(TAG,"Command: Unsure Enter");
    gpio_set_level(triggerPinOut, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinIn, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinIn, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinOut, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinOut, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinIn, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinOut, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinIn, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE+20) / portTICK_PERIOD_MS);
}

/**************************************
 * Corner cases where time is critical *
 **************************************/
 /**
 * Someone is trying to manipulate the count by waving their arm through the barrier towards the inside
 * Sequence is not possible if a person enters
 * expected outcome: no change
 */

void manipulationEnter(){ 
    ESP_LOGI(TAG,"Command: Manipulation Enter ");
    gpio_set_level(triggerPinOut,1);
    vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinOut,0);
    vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_PERIOD_MS);

    gpio_set_level(triggerPinIn, 1);
    vTaskDelay((FASTER_MOVEMENT_THAN_HUMAN/4 + 5) / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinIn, 0);
    vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE + 20) / portTICK_PERIOD_MS);
}

/**********************************************
 * Corner cases that invlove multiple people *
 **********************************************/

/** 
 * Corner case from Group11:
 * Alice peeks into the room, shortly afterwards Bob leaves the room
 * expected count result: -1
 */

void peekIntoandLeaveG11(){
	ESP_LOGI(TAG,"Command: Peek into and leave");
	gpio_set_level(triggerPinOut,1);
	vTaskDelay((TIME_BEFORE_CONSIDERED_AN_OBSTRUCTION-100) / portTICK_PERIOD_MS);
	gpio_set_level(triggerPinOut,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE+20) / portTICK_PERIOD_MS); //peeking finished

    //another person exiting
	gpio_set_level(triggerPinIn,1);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	gpio_set_level(triggerPinIn,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE-20) / portTICK_PERIOD_MS);

	gpio_set_level(triggerPinOut,1);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	gpio_set_level(triggerPinOut,0);
	vTaskDelay((ALLOWED_TIME_IN_MIDDLE_STATE+20) / portTICK_PERIOD_MS);
}

/** 
 * successive entering
 * Alice enters the room while Bob also enters
 * expected outcome: +2
 */

 void successiveEnter(){
    ESP_LOGI(TAG,"Command: Successive Enter");
    // first person entering
    gpio_set_level(triggerPinOut,1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinOut,0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    // first person almost inside
    gpio_set_level(triggerPinIn,1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    // second person entering
    gpio_set_level(triggerPinOut,1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    // first person inside
    gpio_set_level(triggerPinIn,0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    // second person entering
    gpio_set_level(triggerPinOut,0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinIn,1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(triggerPinIn,0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
 }