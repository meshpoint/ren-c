/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  t-datatype.c
**  Summary: datatype datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


/***********************************************************************
**
*/	REBINT CT_Datatype(REBVAL *a, REBVAL *b, REBINT mode)
/*
***********************************************************************/
{
	if (mode >= 0) return (VAL_TYPE_KIND(a) == VAL_TYPE_KIND(b));
	return -1;
}


/***********************************************************************
**
*/	REBFLG MT_Datatype(REBVAL *out, REBVAL *data, REBCNT type)
/*
***********************************************************************/
{
	if (!IS_WORD(data)) return FALSE;
	type = VAL_WORD_CANON(data);
	if (type > REB_MAX) return FALSE;
	VAL_SET(out, REB_DATATYPE);
	VAL_TYPE_KIND(out) = cast(enum Reb_Kind, type - 1);
	VAL_TYPE_SPEC(out) = 0;
	return TRUE;
}


/***********************************************************************
**
*/	REBTYPE(Datatype)
/*
***********************************************************************/
{
	REBVAL *value = D_ARG(1);
	REBVAL *arg = D_ARG(2);
	REBACT act;
	enum Reb_Kind kind = VAL_TYPE_KIND(value);
	REBSER *obj;
	REBINT n;

	switch (action) {

	case A_REFLECT:
		n = What_Reflector(arg); // zero on error
		if (n == OF_SPEC) {
			obj = Make_Std_Object_Managed(STD_TYPE_SPEC);
			Set_Object_Values(
				obj,
				BLK_HEAD(
					VAL_TYPE_SPEC(BLK_SKIP(Lib_Context, SYM_FROM_KIND(kind)))
				)
			);
			Val_Init_Object(D_OUT, obj);
		}
		else if (n == OF_TITLE) {
			Val_Init_String(
				D_OUT,
				Copy_Array_Shallow(VAL_SERIES(BLK_HEAD(
					VAL_TYPE_SPEC(BLK_SKIP(Lib_Context, SYM_FROM_KIND(kind)))
				)))
			);
		}
		else
			raise Error_Cannot_Reflect(VAL_TYPE(value), arg);
		break;

	case A_MAKE:
	case A_TO:
		if (kind != REB_DATATYPE) {
			act = Value_Dispatch[kind];
			if (act) return act(call_, action);
			//return R_NONE;
			raise Error_Bad_Make(kind, arg);
		}

		#if !defined(NDEBUG)
			if (
				LEGACY(OPTIONS_GROUP_NOT_PAREN)
				&& IS_WORD(arg)
				&& VAL_WORD_SYM(arg) == SYM_GROUPX
			) {
				VAL_WORD_SYM(arg) = SYM_FROM_KIND(REB_PAREN);
			}
		#endif

		// if (IS_NONE(arg)) return R_NONE;
		if (MT_Datatype(D_OUT, arg, REB_DATATYPE))
			break;

		raise Error_Bad_Make(REB_DATATYPE, arg);

	default:
		raise Error_Illegal_Action(REB_DATATYPE, action);
	}

	return R_OUT;
}
