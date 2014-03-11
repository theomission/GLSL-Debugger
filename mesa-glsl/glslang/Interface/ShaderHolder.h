/*
 * ShaderHolder.h
 *
 *  Created on: 06.09.2013
 */

#ifndef SHADERHOLDER_H_
#define SHADERHOLDER_H_

#include "ShaderLang.h"
#include "mesa/main/mtypes.h"
#include "glsl/list.h"
#include "AstStack.h"

struct sh_symbol_table;

struct AstShader {
	exec_list* head;
	unsigned version;
	const char* source;
	char* name;
	gl_shader_stage stage;
	bool compile_status;
	char* info_log;
	bool is_es;
	sh_symbol_table* symbols;
	ShVariableList globals;
	AstStack path;
};

struct ShaderHolder {
	int debug_options;
	EShLanguage language;
	AstShader** shaders;
	unsigned num_shaders;
	struct gl_context* ctx;
};


#endif /* SHADERHOLDER_H_ */
