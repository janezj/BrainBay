/* -----------------------------------------------------------------------------

BrainBay  Version 2.0, GPL 2003-2017, contact: chris@shifz.org
Expression-Evaluator - Object
Provides an interface to the ExprTk - Mathematical Expression Library

Authors: Jeremy Wilkerson, Chris Veigl, Arash Partow (ExprTk), Janez Jere (glue ExprTk into this project)
Link to ExprTk - Mathematical Expression Library: http://www.partow.net/programming/exprtk/

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; See the
GNU General Public License for more details.


-----------------------------------------------------------------------------  */
// include exprtk first to hack minmax values
// define _SCL_SECURE_NO_WARNINGS to skip warnings in exprtk.hpp
// exprtk.hpp is a large file. so it is included only in this source file

#define _SCL_SECURE_NO_WARNINGS
#include "exprtk.hpp"


#include "brainBay.h"
#include "ob_evaluator_exprtk.h"
#include <string>
#include <stdarg.h>  

static std::string local_prefix("l_");
static std::map<std::string, float> globals;

static std::string lower(const std::string& x) {
	std::string s;
	s.resize(x.size());
	std::transform(x.begin(), x.end(), s.begin(), ::tolower);
	return s;
}

static float* get_var(std::map<std::string, float>& table, const std::string& name) {
	 
	auto search = table.find(name);
	if (search == table.end()) {
		printf("create variable: %s\n", name.c_str());
		table[name]=0;
		search = table.find(name);
	}
	return &(search->second);
}

static void reset(std::map<std::string, float>& table) {
	for (auto i = table.begin(); i != table.end(); i++) {
		i->second = 0;
	}
}

static float rnd() {
	return ::rand() / (RAND_MAX + 1.0);
}

static float breakpoint() {
	SendMessage(ghWndStatusbox, WM_COMMAND, IDC_STOPSESSION, 0);
	InvalidateRect(0, 0, 0);
	return 0;
}

static float clock_s() {
	return clock() / CLOCKS_PER_SEC;	
}


struct Printer : public exprtk::igeneric_function<float> {	
	typedef exprtk::igeneric_function<float>::parameter_list_t parameter_list_t;
	typedef exprtk::igeneric_function<float>::generic_type generic_type;
	typedef generic_type::string_view string_t;
	typedef generic_type::scalar_view scalar_t;

    Printer(): exprtk::igeneric_function<float>("ST") {}

    inline float operator()(parameter_list_t params) {
		string_t _text(params[0]);
		scalar_t _val(params[1]);
		std::string text = exprtk::to_str(_text);
		float val = _val();
		printf("%s %f\n", text.c_str(), val);
        return 0;
    }
};

struct Resolver : public exprtk::parser<float>::unknown_symbol_resolver
{
  std::map<std::string, float>* locals;   

  Resolver(std::map<std::string, float>* locals) { this->locals = locals; mode = e_usrmode_extended; }
  
  virtual bool process(const std::string& _unknown_symbol,
                        exprtk::symbol_table<float>&      symbol_table,
                        std::string&        error_message)
   {
	  std::string unknown_symbol = lower(_unknown_symbol);
	  if (!unknown_symbol.compare(0, local_prefix.size(), local_prefix))
      {
		  float* var = get_var(*locals, unknown_symbol);
		  bool ok = symbol_table.add_variable(unknown_symbol, *var);
		  return ok;
      }
	  else {
 		  float* var = get_var(globals, unknown_symbol);
		  bool ok = symbol_table.add_variable(unknown_symbol, *var);
		  return ok;
	  }
   }
};

const int MAXEXPRLENGTH = 2560;
const int NUMINPUTS = 6;

class EVALEXPRTKOBJ : public BASE_CL
{
	float input[NUMINPUTS];

	std::string expr_str;
	bool valid;

	exprtk::symbol_table<float> symbol_table;
	exprtk::expression<float> exp;

	void setExpression(const char* str);
	std::map<std::string, float> locals;
	Resolver resolver;
	exprtk::parser<float> parser;
	HFONT monoFont;
	Printer printer;
	float __init__;
public:
	EVALEXPRTKOBJ(int num);
	~EVALEXPRTKOBJ() {
		if (monoFont != NULL) {
			DeleteObject(monoFont);
		}
	}
	void update_inports();
	void make_dialog();
	void load(HANDLE hFile);
	void save(HANDLE hFile);
	void incoming_data(int port, float value);
	void work();
	void session_reset() { 
		reset(locals);
		reset(globals);
	}
	friend LRESULT CALLBACK EvalExptkDlgHandler(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};

/**
factory function 
*/
void createEvaluatorExprtk(int global_num_objects, BASE_CL** actobject) 
{
	*actobject = new EVALEXPRTKOBJ(global_num_objects);
	(*actobject)->object_size=sizeof(EVALEXPRTKOBJ);
}

EVALEXPRTKOBJ::EVALEXPRTKOBJ(int num) : BASE_CL(), resolver(&locals)
{
	monoFont = NULL;

	valid = false;

	outports = 1;
	inports=1;
	width = 70;
	
	for (int i = 0; i < NUMINPUTS; i++) {
		char name[] = { 'A' + i, 0 };

		input[i] = 0;
		strcpy(in_ports[i].in_name, name);
		symbol_table.add_variable(name, input[i]);
	}

	strcpy(out_ports[0].out_name,"out");
	symbol_table.add_constant("INVALID_VALUE", INVALID_VALUE);
	symbol_table.add_constant("PACKETS_PER_SECOND", PACKETSPERSECOND);
	symbol_table.add_constants();
	symbol_table.add_function("rand", rnd);
	symbol_table.add_function("clock", clock_s);
	symbol_table.add_function("breakpoint", breakpoint);
	symbol_table.add_function("print", printer);
	symbol_table.add_variable("init", __init__);
	

	exp.register_symbol_table(symbol_table);

	parser.enable_unknown_symbol_resolver(resolver);

	setExpression("A");
}

void EVALEXPRTKOBJ::update_inports()
{
	int i = std::min(count_inports(this), NUMINPUTS);

	if (i > inports) {
		inports = i;
	}

	height=CON_START+inports*CON_HEIGHT+5;
	InvalidateRect(ghWndMain,NULL,TRUE);
}

void EVALEXPRTKOBJ::make_dialog()
{
	display_toolbox(hDlg=CreateDialog(hInst, (LPCTSTR)IDD_EVALBOX_ML, ghWndStatusbox, (DLGPROC)EvalExptkDlgHandler));
	monoFont = CreateFont(0,12,0,0,0,0,0,0,0,0,0,0,0,TEXT("Courier New"));
	SendMessage(GetDlgItem(hDlg, IDC_EVALEXPRESSION),WM_SETFONT,(WPARAM)monoFont,0);
}

static char* prop_name = "expression_exprtk";

// https://stackoverflow.com/questions/5343190/how-do-i-replace-all-instances-of-a-string-with-another-string
static std::string replace(std::string subject, const std::string& search,
                          const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
         subject.replace(pos, search.length(), replace);
         pos += replace.length();
    }
    return subject;
}
	

// https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
static std::string string_format(const std::string fmt, ...) {
    int size = ((int)fmt.size()) * 2 + 50;   // Use a rubric appropriate for your code
    std::string str;
    va_list ap;
    while (1) {     // Maximum two passes on a POSIX system...
        str.resize(size);
        va_start(ap, fmt);
        int n = vsnprintf((char *)str.data(), size, fmt.c_str(), ap);
        va_end(ap);
        if (n > -1 && n < size) {  // Everything worked
            str.resize(n);
            return str;
        }
        if (n > -1)  // Needed size returned
            size = n + 1;   // For null char
        else
            size *= 2;      // Guess at a larger size (OS specific)
    }
    return str;
}


void EVALEXPRTKOBJ::load(HANDLE hFile) 
{
	char expr[MAXEXPRLENGTH];

	load_object_basics(this);
	load_property(prop_name,P_STRING,expr);
	std::string s = replace(expr, "\\n", "\r\n"); 
	setExpression(s.c_str());
}

void EVALEXPRTKOBJ::save(HANDLE hFile) 
{
	save_object_basics(hFile, this);
	std::string s = replace(expr_str, "\r\n", "\\n");
	save_property(hFile, prop_name,P_STRING, (void*)s.c_str());
}

void EVALEXPRTKOBJ::incoming_data(int port, float value)
{
	// value could be invalid value, the main reason: expression must be responsible, to handle all possible inputs
	input[port] = value;
}

void EVALEXPRTKOBJ::setExpression(const char *str)
{
	__init__ = 1;
	expr_str.assign(str);
	valid = parser.compile(expr_str, exp);
	__init__ = 0;

	if (!valid) {
		std::string err = string_format("Error: %s\nExpression: %s\n",
                   parser.error().c_str(),
                   expr_str.c_str());
		report_error((char*)err.c_str());
	} 
}

void EVALEXPRTKOBJ::work()
{
	float result = valid ? exp.value() : INVALID_VALUE;
	if (valid && exp.results().count()) {
		auto v = exp.results();
		result = *(float*)v[0].data;
	}
	pass_values(0, result);
}

LRESULT CALLBACK EvalExptkDlgHandler(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	EVALEXPRTKOBJ * st = (EVALEXPRTKOBJ *) actobject;

	if (st==NULL || st->type!=OB_EVAL_EXPRTK) 
		return FALSE;

	switch( message ) {
	case WM_INITDIALOG:
		SetDlgItemText(hDlg, IDC_EVALEXPRESSION, st->expr_str.c_str());
		st->monoFont = CreateFont(0,0,0,0,0,0,0,0,0,0,0,0,0,TEXT("Courier New"));
		SendMessage(GetDlgItem(hDlg, IDC_EVALEXPRESSION),WM_SETFONT,(WPARAM)st->monoFont,0);
		return TRUE;
	case WM_CLOSE:
		EndDialog(hDlg, LOWORD(wParam));
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_EVALAPPLY:
			char strExpr[MAXEXPRLENGTH+1];
			GetDlgItemText(hDlg, IDC_EVALEXPRESSION, strExpr, MAXEXPRLENGTH);
			st->setExpression(strExpr);
			strncpy(st->tag, st->expr_str.c_str(),12);			
			st->tag[12]='.';st->tag[13]='.';st->tag[14]=0;
			InvalidateRect(ghWndDesign,NULL,TRUE);
			break;
		}
		return TRUE;
	case WM_SIZE:
	case WM_MOVE:  
		update_toolbox_position(hDlg);
		break;
	}
	return FALSE;
}
