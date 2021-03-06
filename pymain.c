/*

WARNING!  This file is VEERRRY messy and is loosely based on the teensy\main.c file from micropython repository.

It contains some standard library functions that are needed as we are building using -nodefaultlibs.
These should really be separated out and added to libmpython.a

void python_setup(void)
is the function of most interest as this is where user defined C functions and objects are mapped into the python runtime.

*/


#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ENUM_SIZE_TYPE

#include "nlr.h"
#include "misc.h"
#include "mpconfig.h"
// #include "mpqstr.h"
#include "qstr.h"
#include "lexer.h"
#include "lexermemzip.h"
#include "parse.h"
#include "obj.h"
#include "parsehelper.h"
#include "compile.h"
#include "runtime0.h"
#include "runtime.h"
#include "repl.h"
#include "servo.h"
#include "usb.h"
#include "gc.h"
#include "gccollect.h"
#include "led.h"

#include "Arduino.h"

#define CW 0
#define CCW 1
#define FORWARD 0
#define REVERSE 1

bool do_file(const char *filename);

// prototypes from sdfileio.cpp
void stdout_print_strn_serial(void *data, const char *str, unsigned int len);
void stdout_print_strn_serial3(void *data, const char *str, unsigned int len); 
void sd_dir(const char * path);
void sd_type(const char * path);
void sd_hex_dump(const char * path);
void * cpp_lexer_new_from_file(const char *filename);
int cpp_import_stat(char *filename);

void flash_error(int n) {
    for (int i = 0; i < n; i++) {
        led_state(PYB_LED_BUILTIN, 1);
        delay(250);
        led_state(PYB_LED_BUILTIN, 0);
        delay(250);
    }
}


static const char *help_text = "Hi!\n";

/*
static const char *help_text =
"Welcome to Micro Python!\n\n"
"This is a *very* early version of Micro Python and has minimal functionality.\n\n"
"Specific commands for the board:\n"
"    pyb.info()             -- print some general information\n"
"    pyb.gc()               -- run the garbage collector\n"
"    pyb.delay(<n>)         -- wait for n milliseconds\n"
"    pyb.Led(<n>)           -- create Led object for LED n (n=0)\n"
"                              Led methods: on(), off()\n"
"    pyb.gpio(<pin>)        -- read gpio pin\n"
"    pyb.gpio(<pin>, <val>) -- set gpio pin\n"
#if 0
"    pyb.Servo(<n>) -- create Servo object for servo n (n=1,2,3,4)\n"
"                      Servo methods: angle(<x>)\n"
"    pyb.switch()   -- return True/False if switch pressed or not\n"
"    pyb.accel()    -- get accelerometer values\n"
"    pyb.rand()     -- get a 16-bit random number\n"
#endif
;
*/

static bool repl_display_debugging_info = 0;

/*
static mp_obj_t pyb_set_repl_info(mp_obj_t o_value) {
    repl_display_debugging_info = mp_obj_get_int(o_value);
    return mp_const_none;
}
*/

mp_obj_t pyb_analog_read(mp_obj_t pin_obj) {
    uint pin = mp_obj_get_int(pin_obj);
    int val = analogRead(pin);
    return MP_OBJ_NEW_SMALL_INT(val);
}

mp_obj_t pyb_analog_write(mp_obj_t pin_obj, mp_obj_t val_obj) {
    uint pin = mp_obj_get_int(pin_obj);
    int val = mp_obj_get_int(val_obj);
    analogWrite(pin, val);
    return mp_const_none;
}

mp_obj_t pyb_analog_write_resolution(mp_obj_t res_obj) {
    int res = mp_obj_get_int(res_obj);
    analogWriteResolution(res);
    return mp_const_none;
}

mp_obj_t pyb_analog_write_frequency(mp_obj_t pin_obj, mp_obj_t freq_obj) {
    uint pin = mp_obj_get_int(pin_obj);
    int freq = mp_obj_get_int(freq_obj);
    analogWriteFrequency(pin, freq);
    return mp_const_none;
}

// get some help about available functions
static mp_obj_t pyb_help(void) {
    printf("%s", help_text);
    return mp_const_none;
}

// get lots of info about the board
static mp_obj_t pyb_info(void) {
    // get and print unique id; 96 bits
    {
        byte *id = (byte*)0x40048058;
        printf("ID=%02x%02x%02x%02x:%02x%02x%02x%02x:%02x%02x%02x%02x\n", id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11]);
    }

    // get and print clock speeds
    printf("CPU=%u\nBUS=%u\nMEM=%u\n", F_CPU, F_BUS, F_MEM);

    // to print info about memory
    {
        extern void *_sdata;
        extern void *_edata;
        extern void *_sbss;
        extern void *_ebss;
        extern void *_estack;
        extern void *_etext;
        printf("_sdata=%p\n", &_sdata);
        printf("_edata=%p\n", &_edata);
        printf("_sbss=%p\n", &_sbss);
        printf("_ebss=%p\n", &_ebss);
        printf("_estack=%p\n", &_estack);
        printf("_etext=%p\n", &_etext);
        printf("_heap_start=%p\n", &_heap_start);
    }

    // GC info
    {
        gc_info_t info;
        gc_info(&info);
        printf("GC:\n");
        printf("  %lu total\n", info.total);
        printf("  %lu used %lu free\n", info.used, info.free);
        printf("  1=%lu 2=%lu m=%lu\n", info.num_1block, info.num_2block, info.max_block);
    }

#if 0
    // free space on flash
    {
        DWORD nclst;
        FATFS *fatfs;
        f_getfree("0:", &nclst, &fatfs);
        printf("LFS free: %u bytes\n", (uint)(nclst * fatfs->csize * 512));
    }
#endif

    return mp_const_none;
}

#define RAM_START (0x1FFF8000) // fixed for chip
#define HEAP_END  (0x20006000) // tunable
#define RAM_END   (0x20008000) // fixed for chip

void gc_helper_get_regs_and_clean_stack(machine_uint_t *regs, machine_uint_t heap_end);

void gc_collect(void) {
    uint32_t start = millis();
    gc_collect_start();
//  gc_collect_root((void**)&_ram_start, ((uint32_t)&_heap_start - (uint32_t)&_ram_start) / sizeof(uint32_t));
	gc_collect_root((void**)RAM_START, ((uint32_t)&_heap_start - RAM_START) / sizeof(uint32_t));
    machine_uint_t regs[10];
    gc_helper_get_regs_and_clean_stack(regs, HEAP_END);
    gc_collect_root((void**)HEAP_END, (RAM_END - HEAP_END) / sizeof(uint32_t)); // will trace regs since they now live in this function on the stack
    gc_collect_end();
    uint32_t ticks = millis() - start; // TODO implement a function that does this properly

    if (0) {
        // print GC info
        gc_info_t info;
        gc_info(&info);
        printf("GC@%lu %lums\n", start, ticks);
        printf(" %lu total\n", info.total);
        printf(" %lu : %lu\n", info.used, info.free);
        printf(" 1=%lu 2=%lu m=%lu\n", info.num_1block, info.num_2block, info.max_block);
    }
}

static mp_obj_t pyb_gc(void) {
    gc_collect();
    return mp_const_none;
}

MP_DEFINE_CONST_FUN_OBJ_0(pyb_gc_obj, pyb_gc);

mp_obj_t pyb_gpio(uint n_args, mp_obj_t *args) {
    //assert(1 <= n_args && n_args <= 2);

    uint pin = mp_obj_get_int(args[0]);
    if (pin > CORE_NUM_DIGITAL) {
        goto pin_error;
    }

    if (n_args == 1) {
        // get pin
        pinMode(pin, INPUT);
        return MP_OBJ_NEW_SMALL_INT(digitalRead(pin));
    }
    
    // set pin
    pinMode(pin, OUTPUT);
    digitalWrite(pin, rt_is_true(args[1]));
    return mp_const_none;

pin_error:
    nlr_jump(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "pin %d does not exist", (void *)(machine_uint_t)pin));
}

MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_gpio_obj, 1, 2, pyb_gpio);

#if 0
mp_obj_t pyb_hid_send_report(mp_obj_t arg) {
	mp_obj_t *items = mp_obj_get_array_fixed_n(arg, 4);
	uint8_t data[4];
	data[0] = mp_obj_get_int(items[0]);
	data[1] = mp_obj_get_int(items[1]);
	data[2] = mp_obj_get_int(items[2]);
	data[3] = mp_obj_get_int(items[3]);
	usb_hid_send_report(data);
	return mp_const_none;
}
#endif

static qstr pyb_config_source_dir = 0;
static qstr pyb_config_main = 0;

mp_obj_t pyb_source_dir(mp_obj_t source_dir) {
    if (MP_OBJ_IS_QSTR(source_dir)) {
        pyb_config_source_dir = (qstr)source_dir;
    }
    return mp_const_none;
}

mp_obj_t pyb_main(mp_obj_t main) {
    if (MP_OBJ_IS_QSTR(main)) {
        pyb_config_main = (qstr)main;
    }
    return mp_const_none;
}

mp_obj_t pyb_delay(mp_obj_t count) {
    delay(mp_obj_get_int(count));
    return mp_const_none;
}

mp_obj_t pyb_led(mp_obj_t state) {
    led_state(PYB_LED_BUILTIN, rt_is_true(state));
    return state;
}

mp_obj_t pyb_run(mp_obj_t filename_obj) {
	
    // const char *filename = qstr_str(mp_obj_get_qstr(filename_obj));
	if (MP_OBJ_IS_STR(filename_obj)) {
		const char *filename = mp_obj_str_get_str(filename_obj);
		// const char *filename = filename_obj;
		do_file(filename);
	}
    return mp_const_none;
}

mp_obj_t pyb_dir(uint n_args, const mp_obj_t *args)
{
	if (n_args==0)
	{
		// dir current folder
		sd_dir(NULL);
	}
	else
	{
		if (MP_OBJ_IS_STR(args[0])) 
		{
			const char *path = mp_obj_str_get_str(args[0]);
			sd_dir(path);
		}
	}
    return mp_const_none;
}

mp_obj_t pyb_type(mp_obj_t filename_obj) 
{
	if (MP_OBJ_IS_STR(filename_obj)) 
	{
		const char *filename = mp_obj_str_get_str(filename_obj);
		sd_type(filename);
	}
    return mp_const_none;
}

mp_obj_t pyb_hex_dump(mp_obj_t filename_obj) 
{
	if (MP_OBJ_IS_STR(filename_obj)) 
	{
		const char *filename = mp_obj_str_get_str(filename_obj);
		sd_hex_dump(filename);
	}
    return mp_const_none;
}


void * set_stdout_callback(void (*fn)(void *, const char *, unsigned int ));

mp_obj_t pyb_test_stdout( void )
{
	printf("Starting stdout test\n");

	void * old_f = set_stdout_callback(stdout_print_strn_serial);
	
	printf("TESTING SERIAL\n");

	set_stdout_callback(stdout_print_strn_serial3);

	printf("TESTING BLUETOOTH\n");

	set_stdout_callback(old_f);

	printf("TESTING ORIGINAL\n");

	return mp_const_none;
}


char *strdup(const char *str) {
    uint32_t len = strlen(str);
    char *s2 = m_new(char, len + 1);
    memcpy(s2, str, len);
    s2[len] = 0;
    return s2;
}

#define READLINE_HIST_SIZE (8)

static const char *readline_hist[READLINE_HIST_SIZE] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

void stdout_tx_str(const char *str) {
//    usart_tx_str(str);
	usb_vcp_send_str(str);
}

int readline(vstr_t *line, const char *prompt) {
    stdout_tx_str(prompt);
    int len = vstr_len(line);
    int escape = 0;
    int hist_num = 0;
    for (;;) {
        char c;
        for (;;) {
            if (usb_vcp_rx_any() != 0) {
                c = usb_vcp_rx_get();
                break;
#if 0
            } else if (usart_rx_any()) {
                c = usart_rx_char();
                break;
#endif
            }
            //delay(1);
            //if (storage_needs_flush()) {
            //    storage_flush();
            //}
        }
        if (escape == 0) {
            if (c == 4 && vstr_len(line) == len) {
                return 0;
            } else if (c == '\r') {
                stdout_tx_str("\r\n");
                for (int i = READLINE_HIST_SIZE - 1; i > 0; i--) {
                    readline_hist[i] = readline_hist[i - 1];
                }
                readline_hist[0] = strdup(vstr_str(line));
                return 1;
            } else if (c == 27) {
                escape = true;
            } else if (c == 127) {
                if (vstr_len(line) > len) {
                    vstr_cut_tail(line, 1);
                    stdout_tx_str("\b \b");
                }
            } else if (32 <= c && c <= 126) {
                vstr_add_char(line, c);
                stdout_tx_str(line->buf + line->len - 1);
            }
        } else if (escape == 1) {
            if (c == '[') {
                escape = 2;
            } else {
                escape = 0;
            }
        } else if (escape == 2) {
            escape = 0;
            if (c == 'A') {
                // up arrow
                if (hist_num < READLINE_HIST_SIZE && readline_hist[hist_num] != NULL) {
                    // erase line
                    for (int i = line->len - len; i > 0; i--) {
                        stdout_tx_str("\b \b");
                    }
                    // set line to history
                    line->len = len;
                    vstr_add_str(line, readline_hist[hist_num]);
                    // draw line
                    stdout_tx_str(readline_hist[hist_num]);
                    // increase hist num
                    hist_num += 1;
                }
            }
        } else {
            escape = 0;
        }
        delay(10);
    }
}

#if 0 // memzip implimentation...

bool do_file(const char *filename) {
    mp_lexer_t *lex = mp_lexer_new_from_memzip_file(filename);

    if (lex == NULL) {
        printf("could not open file '%s' for reading\n", filename);
        return false;
    }

    qstr parse_exc_id;
    const char *parse_exc_msg;
    mp_parse_node_t pn = mp_parse(lex, MP_PARSE_FILE_INPUT, &parse_exc_id, &parse_exc_msg);
    qstr source_name = mp_lexer_source_name(lex);

    if (pn == MP_PARSE_NODE_NULL) {
        // parse error
        mp_lexer_show_error_pythonic_prefix(lex);
        printf("%s: %s\n", qstr_str(parse_exc_id), parse_exc_msg);
        mp_lexer_free(lex);
        return false;
    }
	
	mp_lexer_free(lex);

    mp_obj_t module_fun = mp_compile(pn, source_name, false);
    mp_parse_node_free(pn);

    if (module_fun == mp_const_none) {
        return false;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        rt_call_function_0(module_fun);
        nlr_pop();
        return true;
    } else {
        // uncaught exception
        mp_obj_print_exception((mp_obj_t)nlr.ret_val);
        return false;
    }
}

#endif

///////////////// SD card ///////////////////////////

int cpp_file_buf_next_char(void *fb);
void cpp_file_buf_close(void *fb);
void * cpp_lexer_new_from_file(const char *filename);
int cpp_file_buf_next_char(void *vfb);

bool parse_compile_execute(mp_lexer_t *lex, mp_parse_input_kind_t input_kind, bool is_repl) {
    mp_parse_error_kind_t parse_error_kind;
    mp_parse_node_t pn = mp_parse(lex, input_kind, &parse_error_kind);
    qstr source_name = mp_lexer_source_name(lex);

    if (pn == MP_PARSE_NODE_NULL) {
        // parse error
        mp_parse_show_exception(lex, parse_error_kind);
        mp_lexer_free(lex);
        return false;
    }

    mp_lexer_free(lex);

    mp_obj_t module_fun = mp_compile(pn, source_name, is_repl);
    mp_parse_node_free(pn);

    if (module_fun == mp_const_none) {
        return false;
    }

    nlr_buf_t nlr;
    bool ret;
    //! uint32_t start = sys_tick_counter;
	uint32_t start = millis();
    if (nlr_push(&nlr) == 0) {
        //! usb_vcp_set_interrupt_char(VCP_CHAR_CTRL_C); // allow ctrl-C to interrupt us
        rt_call_function_0(module_fun);
        //! usb_vcp_set_interrupt_char(VCP_CHAR_NONE); // disable interrupt
        nlr_pop();
        ret = true;
    } else {
        // uncaught exception
        // FIXME it could be that an interrupt happens just before we disable it here
        //! usb_vcp_set_interrupt_char(VCP_CHAR_NONE); // disable interrupt
        mp_obj_print_exception((mp_obj_t)nlr.ret_val);
        ret = false;
    }

    // display debugging info if wanted
    if (is_repl && repl_display_debugging_info) {
        //! uint32_t ticks = sys_tick_counter - start; // TODO implement a function that does this properly
		uint32_t ticks = millis() - start; // TODO implement a function that does this properly
        printf("took %lu ms\n", ticks);
        gc_collect();
        // qstr info
        {
            uint n_pool, n_qstr, n_str_data_bytes, n_total_bytes;
            qstr_pool_info(&n_pool, &n_qstr, &n_str_data_bytes, &n_total_bytes);
            printf("qstr:\n  n_pool=%u\n  n_qstr=%u\n  n_str_data_bytes=%u\n  n_total_bytes=%u\n", n_pool, n_qstr, n_str_data_bytes, n_total_bytes);
        }

        // GC info
        {
            gc_info_t info;
            gc_info(&info);
            printf("GC:\n");
            printf("  %lu total\n", info.total);
            printf("  %lu : %lu\n", info.used, info.free);
            printf("  1=%lu 2=%lu m=%lu\n", info.num_1block, info.num_2block, info.max_block);
        }
    }

    return ret;
}

bool do_file(const char *filename) {

    mp_lexer_t *lex = mp_lexer_new_from_file(filename);

    if (lex == NULL) {
        printf("could not open file '%s' for reading\n", filename);
        return false;
    }

	return parse_compile_execute(lex, MP_PARSE_FILE_INPUT, false);
}

////////////////////////////////////////////////////////////////////

mp_obj_t find_python_object( const char * name )
{
	mp_obj_t o = rt_load_name( qstr_from_str(name) );
	if (o == mp_const_none) 
	{
		printf("robot not found\n");
		return NULL;
	}
	return o;
}

mp_obj_t find_python_attribute( mp_obj_t o, const char * attrib_name )
{
	nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) 
	{
		mp_obj_t attr = rt_load_attr(o, qstr_from_str(attrib_name));
        nlr_pop();
		if (attr != MP_OBJ_NULL && attr != mp_const_none) 
			return attr;
	}
	else
	{
        mp_obj_print_exception((mp_obj_t)nlr.ret_val);
		gc_collect();
	}
	return NULL;
}

mp_obj_t find_python_method( mp_obj_t o, const char * method_name )
{
	nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) 
	{
		mp_obj_t fun;
		rt_load_method(o,qstr_from_str(method_name ),&fun);
        nlr_pop();
		if (fun != MP_OBJ_NULL && fun != mp_const_none) 
			return fun;
	}
	else
	{
        mp_obj_print_exception((mp_obj_t)nlr.ret_val);
		gc_collect();
	}
	return NULL;
}

int python_call_method( mp_obj_t m, mp_obj_t fun )
{
	nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) 
	{
		mp_obj_t ret = rt_call_function_1(fun,m);
		nlr_pop();
		if (ret != MP_OBJ_NULL && ret != mp_const_none) 
		{
			return mp_obj_get_int(ret);
		}
	}
	else
	{
        mp_obj_print_exception((mp_obj_t)nlr.ret_val);
		gc_collect();
	}
	return 0;
}

void run_python_cmd_str( const char * cmd )
{
	// mp_lexer_t *lex = mp_lexer_new_from_memzip_file(filename);
	mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, cmd, strlen(cmd), 0);

	if (lex == NULL) 
	{
		printf("Error creating parser\n");
		return;
	}

	//! qstr parse_exc_id;
	//! const char *parse_exc_msg;
	//! mp_parse_node_t pn = mp_parse(lex, MP_PARSE_SINGLE_INPUT, &parse_exc_id, &parse_exc_msg);
	mp_parse_error_kind_t parse_error_kind;

	mp_parse_node_t pn = mp_parse(lex, MP_PARSE_SINGLE_INPUT, &parse_error_kind);
	qstr source_name = mp_lexer_source_name(lex);

	if (pn == MP_PARSE_NODE_NULL) 
	{
		// parse error
		//! mp_lexer_show_error_pythonic_prefix(lex);
		//! printf("%s: %s\n", qstr_str(parse_exc_id), parse_exc_msg);
		mp_parse_show_exception(lex, parse_error_kind);
		mp_lexer_free(lex);
	}
	else
	{
		// parse okay
		mp_lexer_free(lex);
        mp_obj_t module_fun = mp_compile(pn, source_name, true);
        mp_parse_node_free(pn);
        if (module_fun != mp_const_none) 
		{
            nlr_buf_t nlr;
			uint32_t start = millis();
            if (nlr_push(&nlr) == 0) 
			{
                rt_call_function_0(module_fun);
                nlr_pop();
            } 
			else 
			{
                // uncaught exception
                mp_obj_print_exception((mp_obj_t)nlr.ret_val);
            }

            // display debugging info if wanted
            if (repl_display_debugging_info) {
//                    uint32_t ticks = sys_tick_counter - start; // TODO implement a function that does this properly
				uint32_t ticks = millis() - start; // TODO implement a function that does this properly
                printf("took %lu ms\n", ticks);
                gc_collect();
                pyb_info();
            }
        }
    }
	// stdout_tx_str("\r\n");
	printf("\r");
}

void do_repl(void) {
#if defined(USE_HOST_MODE) && MICROPY_HW_HAS_LCD
    // in host mode, we enable the LCD for the repl
    mp_obj_t lcd_o = rt_call_function_0(rt_load_name(qstr_from_str("LCD")));
    rt_call_function_1(rt_load_attr(lcd_o, qstr_from_str("light")), mp_const_true);
#endif

    stdout_tx_str("Micro Python for Teensy 3.1\r\n");
    stdout_tx_str("Type \"help()\" for more information.\r\n");

    vstr_t line;
    vstr_init(&line, 32);

    for (;;) {
        vstr_reset(&line);
        int ret = readline(&line, ">>> ");
        if (ret == 0) {
            // EOF
            break;
        }

        if (vstr_len(&line) == 0) {
            continue;
        }

        if (mp_repl_is_compound_stmt(vstr_str(&line))) {
            for (;;) {
                vstr_add_char(&line, '\n');
                int len = vstr_len(&line);
                int ret = readline(&line, "... ");
                if (ret == 0 || vstr_len(&line) == len) {
                    // done entering compound statement
                    break;
                }
            }
        }

        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, vstr_str(&line), vstr_len(&line), 0);
        //! qstr parse_exc_id;
        //! const char *parse_exc_msg;
		
		mp_parse_error_kind_t parse_error_kind;

        //! mp_parse_node_t pn = mp_parse(lex, MP_PARSE_SINGLE_INPUT, &parse_exc_id, &parse_exc_msg);
		mp_parse_node_t pn = mp_parse(lex, MP_PARSE_SINGLE_INPUT, &parse_error_kind);
        qstr source_name = mp_lexer_source_name(lex);

        if (pn == MP_PARSE_NODE_NULL) {
            // parse error
            //mp_lexer_show_error_pythonic_prefix(lex);
            //printf("%s: %s\n", qstr_str(parse_exc_id), parse_exc_msg);
			mp_parse_show_exception(lex, parse_error_kind);
            mp_lexer_free(lex);
        } else {
            // parse okay
            mp_lexer_free(lex);
            mp_obj_t module_fun = mp_compile(pn, source_name, true);
            mp_parse_node_free(pn);
            if (module_fun != mp_const_none) {
                nlr_buf_t nlr;
//!                uint32_t start = sys_tick_counter;
				uint32_t start = millis();
                if (nlr_push(&nlr) == 0) {
                    rt_call_function_0(module_fun);
                    nlr_pop();
                } else {
                    // uncaught exception
                    mp_obj_print_exception((mp_obj_t)nlr.ret_val);
                }

                // display debugging info if wanted
                if (repl_display_debugging_info) {
//                    uint32_t ticks = sys_tick_counter - start; // TODO implement a function that does this properly
					uint32_t ticks = millis() - start; // TODO implement a function that does this properly
                    printf("took %lu ms\n", ticks);
                    gc_collect();
                    pyb_info();
                }
            }
        }
    }

    stdout_tx_str("\r\n");
}

void python_setup(void)
{
	// GC init
	gc_init(&_heap_start, (void*)HEAP_END);

	qstr_init();
	rt_init();

    // add some functions to the python namespace
    {
        rt_store_name(QSTR_FROM_STR_STATIC("help"), rt_make_function_n(0,pyb_help));
        mp_obj_t m = mp_obj_new_module(QSTR_FROM_STR_STATIC("pyb"));
        rt_store_attr(m, QSTR_FROM_STR_STATIC("info"), rt_make_function_n(0,pyb_info));
        rt_store_attr(m, QSTR_FROM_STR_STATIC("source_dir"), rt_make_function_n(1,pyb_source_dir));
        rt_store_attr(m, QSTR_FROM_STR_STATIC("main"), rt_make_function_n(1,pyb_main));
//        rt_store_attr(m, QSTR_FROM_STR_STATIC("gc"), rt_make_function_n(0,pyb_gc));
		rt_store_attr(m, MP_QSTR_gc, (mp_obj_t)&pyb_gc_obj);
//        rt_store_attr(m, QSTR_FROM_STR_STATIC("delay"), rt_make_function_n(1,pyb_delay));
//		rt_store_attr(m, MP_QSTR_delay, rt_make_function_n(1,pyb_delay));
        rt_store_attr(m, QSTR_FROM_STR_STATIC("led"), rt_make_function_n(1,pyb_led));
        rt_store_attr(m, QSTR_FROM_STR_STATIC("Led"), rt_make_function_n(1,pyb_Led));
        rt_store_attr(m, QSTR_FROM_STR_STATIC("gpio"), (mp_obj_t)&pyb_gpio_obj);
        rt_store_name(QSTR_FROM_STR_STATIC("pyb"), m);
        rt_store_name(QSTR_FROM_STR_STATIC("run"), rt_make_function_n(1,pyb_run));
		rt_store_name(QSTR_FROM_STR_STATIC("dir"), rt_make_function_var(0,pyb_dir));
		rt_store_name(QSTR_FROM_STR_STATIC("type"), rt_make_function_n(1,pyb_type));
		rt_store_name(QSTR_FROM_STR_STATIC("hex"), rt_make_function_n(1,pyb_hex_dump));
		rt_store_name(MP_QSTR_delay, rt_make_function_n(1,pyb_delay));
    }
}

//
// TODO: Figure out how to get this in libmpython/stubs.c
// Gives: stubs.c:undefined reference to `sqrtf' if we try this?
// 
// 
machine_float_t machine_sqrt(machine_float_t x) {
	// TODO
	// return x;
	return sqrtf(x);
}

mp_import_stat_t mp_import_stat(const char *path)
{
	return (mp_import_stat_t)cpp_import_stat((char*)path);
}

mp_lexer_t *mp_lexer_new_from_file(const char *filename)
{
	// TODO: get rid of mp_lexer_new_from_file
	void * fb = cpp_lexer_new_from_file(filename);
	if (!fb) return NULL;
	return mp_lexer_new(qstr_from_str(filename), fb, (mp_lexer_stream_next_char_t)cpp_file_buf_next_char, (mp_lexer_stream_close_t)cpp_file_buf_close);
}
