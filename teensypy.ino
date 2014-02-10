// need to include mpython here so the micropython includes are availalbe to other modules like pymain.c
#include <mpython.h>
#include "pysystem.h"

#include <SPI.h>
#include <SD.h>
#define SD_CS	15

void setup()
{
	Serial.begin(115200);

	delay(200);

	python_setup();

	Serial.print("Initializing SD card...");
	if (!SD.begin(SD_CS)) 
	{
		Serial.println("failed!");
	}
	else
		Serial.println("OK");

	// run a file 'boot.py' from SD card...
	do_file("boot.py");

	// or execute a python command...
	run_python_cmd_str("print(\"hi!\")");

	// or look for a python object and call a method on it...
	do_file("test.py");
	mp_obj_t o = find_python_object("testob");
	if (o)
	{
		mp_obj_t m = find_python_method( o, "method1");
		if (m) python_call_method(o,m);
	}

	// or enter python command line mode...
	do_repl();

}

void loop()
{
}
