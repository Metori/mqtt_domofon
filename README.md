# MQTT intercom controller

This device can send "door open" command to intercom main unit after receiving of incoming call. It only works with coordinate line intercoms. Tested with Eltis russian intercom.

## Parts used:
* NodeMCU v3 x 1
* Double relay module x1 (or two single relay modules)
* LED x3
* Button x2
* Some resistors
* One diode
* Plastic housing

## Status LED:
* Blinks slowly when Wi-Fi is connecting
* Blinks faster when MQTT client is connecting
* Steady on when ready

## Green and red LEDs:
* Steady on when incoming call detected

## Incoming MQTT messages:
* 'O' - door open command
* 'N' - call reject command (door will not open)
* 'P' - ping command (answers with 'R')

## Outgoing MQTT messages:
* 'R' - ready; sent after successfull boot-up
* 'C' - call; sent after detecting of incoming intercom call
* 'H' - hangup; sent after detected incoming call finished
* 'B' - button; sent when "door open" has been performed by green hw button press
* 'J' - reJected; sent when incoming call has been rejected by red hw button press
* 'S' - success; sent in response to 'O' or 'N' command
* 'F' - fail; sent in response to 'O' or 'N' command (this means that 'O' or 'N' command has been received but no incoming call detected)
* 'L' - last will message; send when device goes offline
