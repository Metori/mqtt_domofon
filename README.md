# MQTT intercom controller

This device can send "door open" command to intercom main unit after receiving of incoming call. It only works with coordinate line intercoms. Tested with Eltis russian intercom.

## Parts used:
* NodeMCU v3 x 1
* Double relay module x1 (or two single relay modules)
* LED x2
* Button x1
* Some resistors
* Plastic housing

## Status LED:
* Blinks slowly when Wi-Fi is connecting
* Blinks faster when MQTT client is connecting
* Steady on when ready

## Call LED:
* Steady on when incoming call detected

## Incoming MQTT messages:
* 'O' - door open command
* 'N' - call reject command (door will not open)

## Outgoing MQTT messages:
* 'R' - ready; send after successfull boot-up
* 'C' - call; send after detecting of incoming intercom call
* 'H' - hangup; send after detected incoming call finished
* 'B' - button; send when "door open" has been performed successfully by hw button press
* 'S' - success; send in response to 'O' or 'N' command
* 'F' - fail; send in response to 'O' or 'N' command (this means that 'O' or 'N' command has been received but no incoming call detected)
