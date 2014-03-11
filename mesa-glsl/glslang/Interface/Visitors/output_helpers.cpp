/*
 * output_helpers.cpp
 *
 *  Created on: 10.03.2014.
 */

#include "output.h"
#include "glsl/list.h"
#include "glslang/Interface/CodeTools.h"
#include "glslang/Interface/SymbolTable.h"
#include "glslang/Interface/AstScope.h"
#include "glsldb/GL/gl.h"
#include <unordered_set>


#define LAYOUTS_COUNT 16
#define LAYOUTS_FIRST_EXPLICIT 11
const char* layouts_names[LAYOUTS_COUNT] = {
	"origin_upper_left", "pixel_center_integer", "depth_any", "depth_greater",
	"depth_less", "depth_unchanged", "std140", "shared",
	"column_major", "row_major", "packed", "location",
	"index", "binding", "offset", "max_vertices"
};
const char* prim_types[7] = {
	"points", "lines", "lines_adjacency", "line_strip",
	"triangles", "triangles_adjacency", "triangle_strip"
};

void ast_output_traverser_visitor::output_qualifier(const struct ast_type_qualifier* q)
{
	const unsigned int layouts[LAYOUTS_COUNT] = {
		q->flags.q.origin_upper_left, q->flags.q.pixel_center_integer,
		q->flags.q.depth_any, q->flags.q.depth_greater,
		q->flags.q.depth_less, q->flags.q.depth_unchanged,
		q->flags.q.std140, q->flags.q.shared,
		q->flags.q.column_major, q->flags.q.row_major,
		q->flags.q.packed, q->flags.q.explicit_location,
		q->flags.q.explicit_index, q->flags.q.explicit_binding,
		q->flags.q.explicit_offset, q->flags.q.max_vertices
	};

	const int explicit_layouts[LAYOUTS_COUNT - LAYOUTS_FIRST_EXPLICIT] = {
		q->location, q->index, q->binding, q->offset, q->max_vertices
	};


	if (q->has_layout()
			|| q->flags.q.max_vertices
			|| q->flags.q.prim_type) {
		ralloc_asprintf_append(&buffer, "layout(");
		bool defined = false;
		for (int i = 0; i < LAYOUTS_COUNT; ++i) {
			if (layouts[i]) {
				if (defined)
					ralloc_asprintf_append(&buffer, ", ");
				ralloc_asprintf_append(&buffer, "%s", layouts_names[i]);
				if (i > LAYOUTS_FIRST_EXPLICIT)
					ralloc_asprintf_append(&buffer, " = %i",
							explicit_layouts[i - LAYOUTS_FIRST_EXPLICIT]);
			}
		}
		// GLSL 1.5 primitive
		if (q->flags.q.prim_type) {
			if (defined)
				ralloc_asprintf_append(&buffer, ", ");
			int ptype;
			switch (q->prim_type) {
			case GL_LINES: ptype = 1; break;
			case GL_LINES_ADJACENCY: ptype = 2; break;
			case GL_LINE_STRIP: ptype = 3; break;
			case GL_TRIANGLES: ptype = 4; break;
			case GL_TRIANGLES_ADJACENCY: ptype = 5; break;
			case GL_TRIANGLE_STRIP: ptype = 6; break;
			default: ptype = 0; break;
			}
			ralloc_asprintf_append(&buffer, "%s", prim_types[ptype]);
		}
		ralloc_asprintf_append(&buffer, ") ");
	}

	if (q->flags.q.constant)
		ralloc_asprintf_append(&buffer, "const ");

	if (q->flags.q.invariant)
		ralloc_asprintf_append(&buffer, "invariant ");

	if (q->flags.q.attribute)
		ralloc_asprintf_append(&buffer, "attribute ");

	if (q->flags.q.varying)
		ralloc_asprintf_append(&buffer, "varying ");

	if (q->flags.q.in && q->flags.q.out) {
		ralloc_asprintf_append(&buffer, "inout ");
	} else {
		if (q->flags.q.in)
			ralloc_asprintf_append(&buffer, "in ");

		if (q->flags.q.out)
			ralloc_asprintf_append(&buffer, "out ");
	}

	if (q->flags.q.centroid)
		ralloc_asprintf_append(&buffer, "centroid ");
	if (q->flags.q.sample)
		ralloc_asprintf_append(&buffer, "sample ");
	if (q->flags.q.uniform)
		ralloc_asprintf_append(&buffer, "uniform ");
	if (q->flags.q.smooth)
		ralloc_asprintf_append(&buffer, "smooth ");
	if (q->flags.q.flat)
		ralloc_asprintf_append(&buffer, "flat ");
	if (q->flags.q.noperspective)
		ralloc_asprintf_append(&buffer, "noperspective ");
}

void ast_output_traverser_visitor::output_sequence(exec_list* list, const char* s,
		const char* j, const char* e, bool do_indent)
{
	ralloc_asprintf_append(&buffer, s);
	if (do_indent){
		depth++;
		indent();
	}
	bool first = true;
	foreach_list_typed(ast_node, node, link, list) {
		if (do_indent)
			indent();
		if (!first)
			ralloc_asprintf_append(&buffer, j);
		node->accept(this);
		first = false;
	}
	if (do_indent) {
		depth--;
		indent();
	}
	ralloc_asprintf_append(&buffer, e);
}

bool ast_output_traverser_visitor::enter(ast_function_expression* node)
{
	if (cgOptions != DBG_CG_ORIGINAL_SRC && node->debug_target()) {
		dbgTargetProcessed = true;

		/* This function call acts as target */
		/* Check if we can use a parameter for debugging */
		int lastInParameter = -1;

		if (!node->is_constructor() && !node->debug_builtin) {
			const char* name = node->subexpressions[0]->primary_expression.identifier;
			ast_function_definition* func = shader->symbols->get_function(name);
			lastInParameter = getFunctionDebugParameter(func);
		}

		if (lastInParameter >= 0) {
			/* we found a usable parameter */
			node->subexpressions[0]->accept(this);
			ralloc_asprintf_append(&buffer, "(");

			int iter = 0;
			foreach_list_typed(ast_node, param, link, &node->expressions) {
				if (iter)
					ralloc_asprintf_append(&buffer, ", ");

				if (iter == lastInParameter) {
					ralloc_asprintf_append(&buffer, "(");
					if (!getSideEffectsDebugParameter(node, lastInParameter)) {
						/* No special care necessary, just add it before */
						cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, 0);
						// FIXME: WTF? WHAT LANGUAGE ORIGIANAL USED?
						ralloc_asprintf_append(&buffer, ", ");
						param->accept(this);
					} else {
						/* Copy to temporary, debug, and copy back */
						cg.addDbgCode(CG_TYPE_PARAMETER, &buffer, cgOptions, 0);
						ralloc_asprintf_append(&buffer, " = (");
						param->accept(this);
						ralloc_asprintf_append(&buffer, "), ");
						cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, 0);
						ralloc_asprintf_append(&buffer, ", ");
						cg.addDbgCode(CG_TYPE_PARAMETER, &buffer, cgOptions, 0);
					}
					ralloc_asprintf_append(&buffer, ")");
				} else
					param->accept(this);
				++iter;
			}
			ralloc_asprintf_append(&buffer, ")");
		} else {
			/* no usable parameter, so debug before function call */
			ralloc_asprintf_append(&buffer, "(");
			cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, 0);
			ralloc_asprintf_append(&buffer, ", ");
			node->subexpressions[0]->accept(this);
			output_sequence(&node->expressions, "(", ", ", ")");
			ralloc_asprintf_append(&buffer, ")");
		}
		return false;
	} else if (cgOptions != DBG_CG_ORIGINAL_SRC && node->debug_target() &&
			node->debug_overwrite != ast_dbg_ow_original) {
		/* This call leads to the actual prosition of debugging */
		const char* name = node->subexpressions[0]->primary_expression.identifier;
		ralloc_asprintf_append(&buffer, ", %s", cg.getDebugName(name));
		output_sequence(&node->expressions, "(", ", ", ")");
		return false;
	} else if (mode == EShLangGeometry && !node->is_constructor() && node->debug_builtin) {
		return geom_call(node);
	}
	return true;
}


bool ast_output_traverser_visitor::geom_call(ast_function_expression *node)
{
	/* Special case for geometry shaders EmitVertex or EndPrimitive */
	int change_type = 0;
	const char* name = node->subexpressions[0]->primary_expression.identifier;
	if (!strcmp(name, END_PRIMITIVE_SIGNATURE))
		change_type = 1;
	else if (strcmp(name, EMIT_VERTEX_SIGNATURE))
		return true;

	switch (this->cgOptions) {
	case DBG_CG_GEOMETRY_MAP:
		node->subexpressions[0]->accept(this);
		output_sequence(&node->expressions, "(", ", ", ");\n");
		indent();
		cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, change_type);
		break;
	case DBG_CG_GEOMETRY_CHANGEABLE:
		/* only for EmitVertex */
		if (change_type)
			break;

		/* Check if changeable in scope here */
		if (cgbls && vl) {
			std::unordered_set<int> changeables;
			for (int id = 0; id < cgbls->numChangeables; id++)
				changeables.emplace(cgbls->changeables[id]->id);

			bool allInScope = true;
			foreach_list(ch_node, &node->changeables) {
				changeable_item* ch_item = (changeable_item*) ch_node;
				if (changeables.find(ch_item->id) != changeables.end())
					continue;

				ShVariable *var = findShVariableFromId(vl, ch_item->id);
				assert(var || !"CodeGen - unknown changeable, stop debugging");
				if (var->builtin)
					continue;
				allInScope = false;
				break;
			}

			if (allInScope)
				cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, CG_GEOM_CHANGEABLE_IN_SCOPE);
			else
				cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, CG_GEOM_CHANGEABLE_NO_SCOPE);

			ralloc_asprintf_append(&buffer, "\n");
			indent();
		}

		/* Add original function call */
		node->subexpressions[0]->accept(this);
		output_sequence(&node->expressions, "(", ", ", ");\n");
		break;
	case DBG_CG_VERTEX_COUNT:
		cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, change_type);
		// TODO:
		//			oit->parseContext->resources->geoOutputType );
		break;
	case DBG_CG_COVERAGE:
	case DBG_CG_CHANGEABLE:
	case DBG_CG_SELECTION_CONDITIONAL:
	case DBG_CG_LOOP_CONDITIONAL:
		break;
	default:
		node->subexpressions[0]->accept(this);
		output_sequence(&node->expressions, "(", ", ", ");\n");
		break;
	}

	return false;
}

void ast_output_traverser_visitor::selection_body(ast_selection_statement* node,
		ast_node* instructions, int debug_option)
{
	if (!instructions)
		return;

   if (node->debug_target() && cgOptions == DBG_CG_SELECTION_CONDITIONAL
		   && node->debug_state_internal == ast_dbg_if_condition_passed)	{
      depth++;
      /* Add code to colorize condition */
      indent();
      //DBG_CG_COVERAGE
      cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, debug_option);
      ralloc_asprintf_append(&buffer, ";\n");
      depth--;
   }

   instructions->accept(this);
   indent();
}


void ast_output_traverser_visitor::loop_debug_prepare(ast_iteration_statement *node)
{
	/* Add loop counter */
	if (node->need_dbgiter())
		ralloc_asprintf_append(&buffer, "%s = 0;\n", node->debug_iter_name);

	/* Add debug temoprary register to copy condition */
	if (node->debug_target() && node->debug_state_internal == ast_dbg_loop_select_flow) {
		switch (cgOptions) {
		case DBG_CG_COVERAGE:
		case DBG_CG_LOOP_CONDITIONAL:
		case DBG_CG_CHANGEABLE:
		case DBG_CG_GEOMETRY_CHANGEABLE:
			indent();
			cg.init(CG_TYPE_CONDITION, NULL, mode);
			cg.addDeclaration(CG_TYPE_CONDITION, &buffer, mode);
			break;
		default:
			break;
		}
	}

	/* Add debug code prior to loop */
	if (node->mode == ast_iteration_statement::ast_for) {
		if (node->debug_target()) {
			if (node->debug_state_internal == ast_dbg_loop_qyr_init) {
				switch (cgOptions) {
				case DBG_CG_COVERAGE:
				case DBG_CG_CHANGEABLE:
				case DBG_CG_GEOMETRY_CHANGEABLE:
					cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, 0);
					ralloc_asprintf_append(&buffer, ";\n");
					break;
				default:
					break;
				}
			}
		} else {
			if (node->debug_state_internal == ast_dbg_loop_wrk_init) {
				depth++;
				ralloc_asprintf_append(&buffer, "{\n");
			}
		}
	}
}

void ast_output_traverser_visitor::loop_debug_condition(ast_iteration_statement* node)
{
	if (cgOptions != DBG_CG_ORIGINAL_SRC && node->debug_target()) {
		if (node->debug_state_internal == ast_dbg_loop_qyr_test) {
			cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, 0);
			ralloc_asprintf_append(&buffer, ", ");
		} else if (node->debug_state_internal == ast_dbg_loop_select_flow) {
			/* Copy test */
			cg.addDbgCode(CG_TYPE_CONDITION, &buffer, cgOptions, 0);
			ralloc_asprintf_append(&buffer, " = ( ");
		}
	}

	/* Add original condition without any modifications */
	DbgCgOptions opts = cgOptions;
	cgOptions = DBG_CG_ORIGINAL_SRC;
	if (node->condition) {
		// Condition here is not-inverted. Invert it again.
		ralloc_asprintf_append(&buffer, "!");
		node->condition->accept(this);
	} else {
		ralloc_asprintf_append(&buffer, "true");
	}
	cgOptions = opts;

	if (cgOptions != DBG_CG_ORIGINAL_SRC && node->debug_target()
			&& node->debug_state_internal == ast_dbg_loop_select_flow) {
		ralloc_asprintf_append(&buffer, "), ");
		/* Add debug code */
		cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, 0);
		ralloc_asprintf_append(&buffer, ", ");
		cg.addDbgCode(CG_TYPE_CONDITION, &buffer, cgOptions, 0);
	}
}

void ast_output_traverser_visitor::loop_debug_terminal(ast_iteration_statement* node)
{
	if (cgOptions != DBG_CG_ORIGINAL_SRC) {
		if (node->mode == ast_iteration_statement::ast_for && node->debug_target()
				&& node->debug_state_internal == ast_dbg_loop_qyr_terminal) {
			cg.addDbgCode(CG_TYPE_RESULT, &buffer, cgOptions, 0);
			if (node->rest_expression)
				ralloc_asprintf_append(&buffer, ", ");
		}
	}

	DbgCgOptions opts = cgOptions;
	cgOptions = DBG_CG_ORIGINAL_SRC;
	if (node->rest_expression)
		node->rest_expression->accept(this);
	cgOptions = opts;
}

void ast_output_traverser_visitor::loop_debug_end(ast_iteration_statement* node)
{
	if (cgOptions == DBG_CG_ORIGINAL_SRC)
		return;

	depth++;
	if (node->need_dbgiter()) {
		indent();
		ralloc_asprintf_append(&buffer, "%s++;\n", node->debug_iter_name);
	}

	if (node->mode == ast_iteration_statement::ast_for && !node->debug_target()
			&& node->debug_state_internal == ast_dbg_loop_wrk_init) {
		depth--;
		indent();
		ralloc_asprintf_append(&buffer, "}\n");
	}
	depth--;
	indent();
}
