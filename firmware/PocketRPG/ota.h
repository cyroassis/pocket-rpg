// Software update over WiFi: the sketch's side (see ota.cpp).
#pragma once
void otaBegin(void (*redraw)());   // redraw is called while it works, to show progress
void otaPoll();                    // every loop pass while the screen is on
void otaStop();                    // screen off: WiFi off right away
