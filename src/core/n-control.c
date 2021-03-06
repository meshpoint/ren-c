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
**  Module:  n-control.c
**  Summary: native functions for control flow
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


// Local flags used for Protect functions below:
enum {
	PROT_SET,
	PROT_DEEP,
	PROT_HIDE,
	PROT_WORD,
	PROT_MAX
};


/***********************************************************************
**
*/	static void Protect_Key(REBVAL *key, REBCNT flags)
/*
***********************************************************************/
{
	if (GET_FLAG(flags, PROT_WORD)) {
		if (GET_FLAG(flags, PROT_SET)) VAL_SET_EXT(key, EXT_WORD_LOCK);
		else VAL_CLR_EXT(key, EXT_WORD_LOCK);
	}

	if (GET_FLAG(flags, PROT_HIDE)) {
		if GET_FLAG(flags, PROT_SET) VAL_SET_EXT(key, EXT_WORD_HIDE);
		else VAL_CLR_EXT(key, EXT_WORD_HIDE);
	}
}


/***********************************************************************
**
*/	static void Protect_Value(REBVAL *value, REBCNT flags)
/*
**		Anything that calls this must call Unmark() when done.
**
***********************************************************************/
{
	if (ANY_SERIES(value) || IS_MAP(value))
		Protect_Series(value, flags);
	else if (IS_OBJECT(value) || IS_MODULE(value))
		Protect_Object(value, flags);
}


/***********************************************************************
**
*/	void Protect_Series(REBVAL *val, REBCNT flags)
/*
**		Anything that calls this must call Unmark() when done.
**
***********************************************************************/
{
	REBSER *series = VAL_SERIES(val);

	if (SERIES_GET_FLAG(series, SER_MARK)) return; // avoid loop

	if (GET_FLAG(flags, PROT_SET))
		PROTECT_SERIES(series);
	else
		UNPROTECT_SERIES(series);

	if (!ANY_BLOCK(val) || !GET_FLAG(flags, PROT_DEEP)) return;

	SERIES_SET_FLAG(series, SER_MARK); // recursion protection

	for (val = VAL_BLK_DATA(val); NOT_END(val); val++) {
		Protect_Value(val, flags);
	}
}


/***********************************************************************
**
*/	void Protect_Object(REBVAL *value, REBCNT flags)
/*
**		Anything that calls this must call Unmark() when done.
**
***********************************************************************/
{
	REBSER *series = VAL_OBJ_FRAME(value);

	if (SERIES_GET_FLAG(series, SER_MARK)) return; // avoid loop

	if (GET_FLAG(flags, PROT_SET)) PROTECT_SERIES(series);
	else UNPROTECT_SERIES(series);

	for (value = FRM_KEYS(series)+1; NOT_END(value); value++) {
		Protect_Key(value, flags);
	}

	if (!GET_FLAG(flags, PROT_DEEP)) return;

	SERIES_SET_FLAG(series, SER_MARK); // recursion protection

	for (value = FRM_VALUES(series)+1; NOT_END(value); value++) {
		Protect_Value(value, flags);
	}
}


/***********************************************************************
**
*/	static void Protect_Word_Value(REBVAL *word, REBCNT flags)
/*
***********************************************************************/
{
	REBVAL *key;
	REBVAL *val;

	if (ANY_WORD(word) && HAS_FRAME(word) && VAL_WORD_INDEX(word) > 0) {
		key = FRM_KEYS(VAL_WORD_FRAME(word)) + VAL_WORD_INDEX(word);
		Protect_Key(key, flags);
		if (GET_FLAG(flags, PROT_DEEP)) {
			// Ignore existing mutability state, by casting away the const.
			// (Most routines should DEFINITELY not do this!)
			val = m_cast(REBVAL*, GET_VAR(word));
			Protect_Value(val, flags);
			Unmark(val);
		}
	}
	else if (ANY_PATH(word)) {
		REBCNT index;
		REBSER *obj;
		if ((obj = Resolve_Path(word, &index))) {
			key = FRM_KEY(obj, index);
			Protect_Key(key, flags);
			if (GET_FLAG(flags, PROT_DEEP)) {
				Protect_Value(val = FRM_VALUE(obj, index), flags);
				Unmark(val);
			}
		}
	}
}


/***********************************************************************
**
*/	static int Protect(struct Reb_Call *call_, REBCNT flags)
/*
**	Common arguments between protect and unprotect:
**
**		1: value
**		2: /deep  - recursive
**		3: /words  - list of words
**		4: /values - list of values
**
**	Protect takes a HIDE parameter as #5.
**
***********************************************************************/
{
	REBVAL *val = D_ARG(1);

	// flags has PROT_SET bit (set or not)

	Check_Security(SYM_PROTECT, POL_WRITE, val);

	if (D_REF(2)) SET_FLAG(flags, PROT_DEEP);
	//if (D_REF(3)) SET_FLAG(flags, PROT_WORD);

	if (IS_WORD(val) || IS_PATH(val)) {
		Protect_Word_Value(val, flags); // will unmark if deep
		return R_ARG1;
	}

	if (IS_BLOCK(val)) {
		if (D_REF(3)) { // /words
			for (val = VAL_BLK_DATA(val); NOT_END(val); val++)
				Protect_Word_Value(val, flags);  // will unmark if deep
			return R_ARG1;
		}
		if (D_REF(4)) { // /values
			REBVAL *val2;
			REBVAL safe;
			for (val = VAL_BLK_DATA(val); NOT_END(val); val++) {
				if (IS_WORD(val)) {
					// !!! Temporary and ugly cast; since we *are* PROTECT
					// we allow ourselves to get mutable references to even
					// protected values so we can no-op protect them.
					val2 = m_cast(REBVAL*, GET_VAR(val));
				}
				else if (IS_PATH(val)) {
					const REBVAL *path = val;
					if (Do_Path(&safe, &path, 0)) {
						val2 = val; // !!! comment said "found a function"
					} else {
						val2 = &safe;
					}
				}
				else
					val2 = val;

				Protect_Value(val2, flags);
				if (GET_FLAG(flags, PROT_DEEP)) Unmark(val2);
			}
			return R_ARG1;
		}
	}

	if (GET_FLAG(flags, PROT_HIDE)) raise Error_0(RE_BAD_REFINES);

	Protect_Value(val, flags);

	if (GET_FLAG(flags, PROT_DEEP)) Unmark(val);

	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(also)
/*
***********************************************************************/
{
	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(all)
/*
***********************************************************************/
{
	REBSER *block = VAL_SERIES(D_ARG(1));
	REBCNT index = VAL_INDEX(D_ARG(1));

	// Default result for 'all []'
	SET_TRUE(D_OUT);

	while (index < SERIES_TAIL(block)) {
		index = Do_Next_May_Throw(D_OUT, block, index);
		if (index == THROWN_FLAG) break;
		// !!! UNSET! should be an error, CC#564 (Is there a better error?)
		/* if (IS_UNSET(D_OUT)) raise Error_0(RE_NO_RETURN); */
		if (IS_CONDITIONAL_FALSE(D_OUT)) {
			SET_TRASH_SAFE(D_OUT);
			return R_NONE;
		}
	}
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(any)
/*
***********************************************************************/
{
	REBSER *block = VAL_SERIES(D_ARG(1));
	REBCNT index = VAL_INDEX(D_ARG(1));

	while (index < SERIES_TAIL(block)) {
		index = Do_Next_May_Throw(D_OUT, block, index);
		if (index == THROWN_FLAG) return R_OUT;

		// !!! UNSET! should be an error, CC#564 (Is there a better error?)
		/* if (IS_UNSET(D_OUT)) raise Error_0(RE_NO_RETURN); */

		if (!IS_CONDITIONAL_FALSE(D_OUT) && !IS_UNSET(D_OUT)) return R_OUT;
	}

	return R_NONE;
}


/***********************************************************************
**
*/	REBNATIVE(apply)
/*
**		1: func
**		2: block
**		3: /only
**
***********************************************************************/
{
	REBVAL * func = D_ARG(1);
	REBVAL * block = D_ARG(2);
	REBOOL reduce = !D_REF(3);

	if (!Apply_Block_Throws(
		D_OUT, func, VAL_SERIES(block), VAL_INDEX(block), reduce, NULL
	)) {
		// No special handling needed if D_OUT is thrown, as we are just
		// returning it to bubble up anyway
	}
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(attempt)
/*
***********************************************************************/
{
	REBOL_STATE state;
	const REBVAL *error;

	PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `raise Error` can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) return R_NONE;

	if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(1)), VAL_INDEX(D_ARG(1)))) {
		// This means that D_OUT is a THROWN() value, but we have
		// no special processing to apply.  Fall through and return it.
	}

	DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(break)
/*
**		1: /with
**		2: value
**		3: /return (deprecated)
**		4: return-value
**
**	While BREAK is implemented via a THROWN() value that bubbles up
**	through the stack, it may not ultimately use the WORD! of BREAK
**	as its /NAME.
**
***********************************************************************/
{
	REBVAL *value = D_REF(1) ? D_ARG(2) : (D_REF(3) ? D_ARG(4) : UNSET_VALUE);

	Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_BREAK);

	CONVERT_NAME_TO_THROWN(D_OUT, value);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(case)
/*
**	1: block
**	2: /all
**	3: /only
**
***********************************************************************/
{
	// We leave D_ARG(1) alone, it is holding 'block' alive from GC
	REBSER *block = VAL_SERIES(D_ARG(1));
	REBCNT index = VAL_INDEX(D_ARG(1));

	// Save refinements to booleans to free up their call frame slots
	REBFLG all = D_REF(2);
	REBFLG only = D_REF(3);

	// reuse refinement slots for GC safety (pointers are optimized out)
	REBVAL * const condition_result = D_ARG(2);
	REBVAL * const body_result = D_ARG(3);

	// CASE is in the same family as IF/UNLESS/EITHER, so if there is no
	// matching condition it will return a NONE!.  Set that as default.

	SET_NONE(D_OUT);

	while (index < SERIES_TAIL(block)) {

		index = Do_Next_May_Throw(condition_result, block, index);

		if (index == THROWN_FLAG) {
			*D_OUT = *condition_result; // is a RETURN, BREAK, THROW...
			return R_OUT;
		}

		if (index == END_FLAG) raise Error_0(RE_PAST_END);

		if (IS_UNSET(condition_result)) raise Error_0(RE_NO_RETURN);

		// We DO the next expression, rather than just assume it is a
		// literal block.  That allows you to write things like:
		//
		//     condition: true
		//     case [condition 10 + 20] ;-- returns 30
		//
		// But we need to DO regardless of the condition being true or
		// false.  Rebol2 would just skip over one item (the 10 in this
		// case) and get an error.  Code not in blocks must be evaluated
		// even if false, as it is with 'if false (print "eval'd")'
		//
		// If the source was a literal block then the Do_Next_May_Throw
		// will *probably* be a no-op, but consider infix operators:
		//
		//     case [true [stuff] + [more stuff]]
		//
		// Until such time as DO guarantees such things aren't legal,
		// CASE must evaluate block literals too.

	#if !defined(NDEBUG)
		if (
			LEGACY(OPTIONS_BROKEN_CASE_SEMANTICS)
			&& IS_CONDITIONAL_FALSE(condition_result)
		) {
			// case [true add 1 2] => 3
			// case [false add 1 2] => 2 ;-- in Rebol2
			index++;

			// forgets the last evaluative result for a TRUE condition
			// when /ALL is set (instead of keeping it to return)
			SET_NONE(D_OUT);
			continue;
		}
	#endif

		index = Do_Next_May_Throw(body_result, block, index);

		if (index == THROWN_FLAG) {
			*D_OUT = *body_result; // is a RETURN, BREAK, THROW...
			return R_OUT;
		}

		if (index == END_FLAG) {
		#if !defined(NDEBUG)
			if (LEGACY(OPTIONS_BROKEN_CASE_SEMANTICS)) {
				// case [first [a b c]] => true ;-- in Rebol2
				return R_TRUE;
			}
		#endif

			// case [first [a b c]] => **error**
			raise Error_0(RE_PAST_END);
		}

		if (IS_CONDITIONAL_TRUE(condition_result)) {

			if (!only && IS_BLOCK(body_result)) {
				// If we're not using the /ONLY switch and it's a block,
				// we'll need two evaluations for things like:
				//
				//     stuff: [print "This will be printed"]
				//     case [true stuff]
				//
				if (Do_Block_Throws(
					D_OUT, VAL_SERIES(body_result), VAL_INDEX(body_result)
				)) {
					return R_OUT;
				}
			}
			else {
				// With /ONLY (or a non-block) don't do more evaluation, so
				// for the above that's: [print "This will be printed"]

				*D_OUT = *body_result;
			}

		#if !defined(NDEBUG)
			if (LEGACY(OPTIONS_BROKEN_CASE_SEMANTICS)) {
				if (IS_UNSET(D_OUT)) {
					// case [true [] false [1 + 2]] => true ;-- in Rebol2
					SET_TRUE(D_OUT);
				}
			}
		#endif

			// One match is enough to return the result now, unless /ALL
			if (!all) return R_OUT;
		}
	}

	// Returns the evaluative result of the last body whose condition was
	// conditionally true, or defaults to NONE if there weren't any

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(catch)
/*
**	1 block
**	2 /name
**	3 name-list
**	4 /quit
**	5 /any
**	6 /with
**	7 handler
**
**	There's a refinement for catching quits, and CATCH/ANY will not
**	alone catch it (you have to CATCH/ANY/QUIT).  The use of the
**	WORD! QUIT is pending review, and when full label values are
**	available it will likely be changed to at least get the native
**	(e.g. equal to THROW with /NAME :QUIT instead of /NAME 'QUIT)
**
***********************************************************************/
{
	REBVAL * const block = D_ARG(1);

	const REBOOL named = D_REF(2);
	REBVAL * const name_list = D_ARG(3);

	// We save the values into booleans (and reuse their GC-protected slots)
	const REBOOL quit = D_REF(4);
	const REBOOL any = D_REF(5);

	const REBOOL with = D_REF(6);
	REBVAL * const handler = D_ARG(7);

	// /ANY would override /NAME, so point out the potential confusion
	if (any && named) raise Error_0(RE_BAD_REFINES);

	if (Do_Block_Throws(D_OUT, VAL_SERIES(block), VAL_INDEX(block))) {
		if (
			(any && (!IS_WORD(D_OUT) || VAL_WORD_SYM(D_OUT) != SYM_QUIT))
			|| (quit && IS_WORD(D_OUT) && VAL_WORD_SYM(D_OUT) == SYM_QUIT)
		) {
			goto was_caught;
		}

		if (named) {
			// We use equal? by way of Compare_Modify_Values, and re-use the
			// refinement slots for the mutable space
			REBVAL * const temp1 = D_ARG(4);
			REBVAL * const temp2 = D_ARG(5);

			// !!! The reason we're copying isn't so the OPT_VALUE_THROWN bit
			// won't confuse the equality comparison...but would it have?

			if (IS_BLOCK(name_list)) {
				// Test all the words in the block for a match to catch
				REBVAL *candidate = VAL_BLK_DATA(name_list);
				for (; NOT_END(candidate); candidate++) {
					// !!! Should we test a typeset for illegal name types?
					if (IS_BLOCK(candidate))
						raise Error_1(RE_INVALID_ARG, name_list);

					*temp1 = *candidate;
					*temp2 = *D_OUT;

					// Return the THROW/NAME's arg if the names match
					// !!! 0 means equal?, but strict-equal? might be better
					if (Compare_Modify_Values(temp1, temp2, 0))
						goto was_caught;
				}
			}
			else {
				*temp1 = *name_list;
				*temp2 = *D_OUT;

				// Return the THROW/NAME's arg if the names match
				// !!! 0 means equal?, but strict-equal? might be better
				if (Compare_Modify_Values(temp1, temp2, 0))
					goto was_caught;
			}
		}
		else {
			// Return THROW's arg only if it did not have a /NAME supplied
			if (IS_NONE(D_OUT))
				goto was_caught;
		}
	}

	return R_OUT;

was_caught:
	if (with) {
		if (IS_BLOCK(handler)) {
			// There's no way to pass args to a block (so just DO it)
			if (Do_Block_Throws(
				D_OUT, VAL_SERIES(handler), VAL_INDEX(handler)
			)) {
				return R_OUT;
			}
			return R_OUT;
		}
		else if (ANY_FUNC(handler)) {
			REBVAL *param = BLK_SKIP(VAL_FUNC_PARAMLIST(handler), 1);

			// We again re-use the refinement slots, but this time as mutable
			// space protected from GC for the handler's arguments
			REBVAL *thrown_arg = D_ARG(4);
			REBVAL *thrown_name = D_ARG(5);

			TAKE_THROWN_ARG(thrown_arg, D_OUT);
			*thrown_name = *D_OUT; // THROWN bit cleared by TAKE_THROWN_ARG

			if (
				(VAL_FUNC_NUM_PARAMS(handler) == 0)
				|| IS_REFINEMENT(VAL_FUNC_PARAM(handler, 1))
			) {
				// If the handler is zero arity or takes a first parameter
				// that is a refinement, call it with no arguments
				//
				if (Apply_Func_Throws(D_OUT, handler, NULL)) {
					// No need to do anything special in the thrown case,
					// as we are just returning the thrown value anyway
				}
			}
			else if (
				(VAL_FUNC_NUM_PARAMS(handler) == 1)
				|| IS_REFINEMENT(VAL_FUNC_PARAM(handler, 2))
			) {
				// If the handler is arity one (with a non-refinement
				// parameter), or a greater arity with a second parameter that
				// is a refinement...call it with *just* the thrown value.
				//
				if (Apply_Func_Throws(D_OUT, handler, thrown_arg, NULL)) {
					// No need to do anything special in the thrown case,
					// as we are just returning the thrown value anyway
				}
			}
			else {
				// For all other handler signatures, try passing both the
				// thrown arg and the thrown name.  Let Apply take care of
				// checking that the arguments are legal for the call.
				//
				if (Apply_Func_Throws(
					D_OUT, handler, thrown_arg, thrown_name, NULL
				)) {
					// No need to do anything special in the thrown case,
					// as we are just returning the thrown value anyway
				}
			}

			return R_OUT;
		}
	}

	// If no handler, just return the caught thing
	TAKE_THROWN_ARG(D_OUT, D_OUT);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(throw)
/*
***********************************************************************/
{
	REBVAL * const value = D_ARG(1);
	REBOOL named = D_REF(2);
	REBVAL * const name_value = D_ARG(3);

	if (IS_ERROR(value)) {
		// We raise an alert from within the implementation of throw for
		// trying to use it to trigger errors, because if THROW just didn't
		// take errors in the spec it wouldn't guide what *to* use.
		//
		raise Error_0(RE_USE_FAIL_FOR_ERROR);

		// Note: Caller can put the ERROR! in a block or use some other
		// such trick if it wants to actually throw an error.
		// (Better than complicating throw with /error-is-intentional!)
	}

	if (named) {
		// blocks as names would conflict with name_list feature in catch
		assert(!IS_BLOCK(name_value));
		*D_OUT = *name_value;
	}
	else {
		// None values serving as representative of THROWN() means "no name"

		// !!! This convention might be a bit "hidden" while debugging if
		// one misses the THROWN() bit.  But that's true of THROWN() values
		// in general.  Debug output should make noise about THROWNs
		// whenever it sees them.

		SET_NONE(D_OUT);
	}

	CONVERT_NAME_TO_THROWN(D_OUT, value);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(comment)
/*
***********************************************************************/
{
	return R_UNSET;
}


/***********************************************************************
**
*/	REBNATIVE(compose)
/*
**		{Evaluates a block of expressions, only evaluating parens, and returns a block.}
**		1: value "Block to compose"
**		2: /deep "Compose nested blocks"
**		3: /only "Inserts a block value as a block"
**		4: /into "Output results into a block with no intermediate storage"
**		5: target
**
**		!!! Should 'compose quote (a (1 + 2) b)' give back '(a 3 b)' ?
**		!!! What about 'compose quote a/(1 + 2)/b' ?
**
***********************************************************************/
{
	REBVAL *value = D_ARG(1);
	REBOOL into = D_REF(4);

	// Only composes BLOCK!, all other arguments evaluate to themselves
	if (!IS_BLOCK(value)) return R_ARG1;

	// Compose expects out to contain the target if /INTO
	if (into) *D_OUT = *D_ARG(5);

	Compose_Block(D_OUT, value, D_REF(2), D_REF(3), into);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(continue)
/*
**	While CONTINUE is implemented via a THROWN() value that bubbles up
**	through the stack, it may not ultimately use the WORD! of CONTINUE
**	as its /NAME.
**
***********************************************************************/
{
	Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_CONTINUE);
	CONVERT_NAME_TO_THROWN(D_OUT, UNSET_VALUE);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(do)
/*
***********************************************************************/
{
	REBVAL * const value = D_ARG(1);
	REBVAL * const args_ref = D_ARG(2);
	REBVAL * const arg = D_ARG(3);
	REBVAL * const next_ref = D_ARG(4);
	REBVAL * const var = D_ARG(5);

#if !defined(NDEBUG)
	if (LEGACY(OPTIONS_DO_RUNS_FUNCTIONS)) {
		switch (VAL_TYPE(value)) {

		case REB_NATIVE:
		case REB_ACTION:
		case REB_COMMAND:
		case REB_REBCODE:
		case REB_CLOSURE:
		case REB_FUNCTION:
			VAL_SET_OPT(value, OPT_VALUE_REEVALUATE);
			return R_ARG1;
		}
	}
#endif

	switch (VAL_TYPE(value)) {
	case REB_NONE:
		// No-op is convenient for `do if ...` constructions
		return R_NONE;

	case REB_BLOCK:
	case REB_PAREN:
		if (D_REF(4)) { // next
			VAL_INDEX(value) = Do_Next_May_Throw(
				D_OUT, VAL_SERIES(value), VAL_INDEX(value)
			);

			if (VAL_INDEX(value) == THROWN_FLAG) {
				// We're going to return the value in D_OUT anyway, but
				// if we looked at D_OUT we'd have to check this first
			}

			if (VAL_INDEX(value) == END_FLAG) {
				VAL_INDEX(value) = VAL_TAIL(value);
				Set_Var(D_ARG(5), value);
				SET_TRASH_SAFE(D_OUT);
				return R_UNSET;
			}
			Set_Var(D_ARG(5), value); // "continuation" of block
			return R_OUT;
		}

		if (Do_Block_Throws(D_OUT, VAL_SERIES(value), 0))
			return R_OUT;

		return R_OUT;

	case REB_BINARY:
	case REB_STRING:
	case REB_URL:
	case REB_FILE:
	case REB_TAG:
		// DO native and system/intrinsic/do* must use same arg list:
		if (Do_Sys_Func_Throws(
			D_OUT,
			SYS_CTX_DO_P,
			value,
			args_ref,
			arg,
			next_ref,
			var,
			NULL
		)) {
			// Was THROW, RETURN, EXIT, QUIT etc...
			// No special handling, just return as we were going to
		}
		return R_OUT;

	case REB_ERROR:
	#if !defined(NDEBUG)
		if (LEGACY(OPTIONS_DO_RAISES_ERRORS))
			raise Error_Is(value);
	#endif
		// This path will no longer raise the error you asked for, though it
		// will still raise *an* error directing you to use FAIL.)
		raise Error_0(RE_USE_FAIL_FOR_ERROR);

	case REB_TASK:
		Do_Task(value);
		return R_ARG1;
	}

	// Note: it is not possible to write a wrapper function in Rebol
	// which can do what EVAL can do for types that consume arguments
	// (like SET-WORD!, SET-PATH! and FUNCTION!).  DO used to do this for
	// functions only, EVAL generalizes it.
	raise Error_0(RE_USE_EVAL_FOR_EVAL);
}


/***********************************************************************
**
*/	REBNATIVE(either)
/*
***********************************************************************/
{
	REBCNT argnum = IS_CONDITIONAL_FALSE(D_ARG(1)) ? 3 : 2;

	if (IS_BLOCK(D_ARG(argnum)) && !D_REF(4) /* not using /ONLY */) {
		if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(argnum)), 0))
			return R_OUT;

		return R_OUT;
	}

	return argnum == 2 ? R_ARG2 : R_ARG3;
}


/***********************************************************************
**
*/	REBNATIVE(eval)
/*
***********************************************************************/
{
	REBVAL * const value = D_ARG(1);

	// Sets special flag, intercepted by the Do_Core() loop and used
	// to signal that it should treat the return value as if it had
	// seen it literally inline at the callsite.
	//
	//     >> x: 10
	//     >> (quote x:) 20
	//     >> print x
	//     10 ;-- the quoted x: is not "live"
	//
	//     >> x: 10
	//     >> eval (quote x:) 20
	//     >> print x
	//     20 ;-- eval "activates" x: so it's as if you'd written `x: 20`
	//
	// This can be used to dispatch arbitrary function values without
	// putting their arguments into blocks.
	//
	//     >> eval :add 10 20
	//     == 30
	//
	// So although eval is just an arity 1 function, it is able to use its
	// argument as a cue for its "actual arity" before the next value is
	// to be evaluated.  This means it is doing something no other Rebol
	// function is able to do.
	//
	// Note: Because it is slightly evil, "eval" is a good name for it.
	// It may confuse people a little because it has no effect on blocks,
	// but that does reinforce the truth that blocks are actually inert.

	VAL_SET_OPT(value, OPT_VALUE_REEVALUATE);
	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(exit)
/*
**	1: /with
**	2: value
**
**	While EXIT is implemented via a THROWN() value that bubbles up
**	through the stack, it may not ultimately use the WORD! of EXIT
**	as its /NAME.
**
***********************************************************************/
{
#if !defined(NDEBUG)
	if (LEGACY(OPTIONS_EXIT_FUNCTIONS_ONLY))
		Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_RETURN);
	else
		Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_EXIT);
#else
	Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_EXIT);
#endif

	CONVERT_NAME_TO_THROWN(D_OUT, D_REF(1) ? D_ARG(2) : UNSET_VALUE);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(fail)
/*
***********************************************************************/
{
	REBVAL * const reason = D_ARG(1);

	if (IS_ERROR(reason)) {
		raise Error_Is(reason);
	}
	else if (IS_STRING(reason) || IS_BLOCK(reason)) {
		// Ultimately we'd like FAIL to use some clever error-creating
		// dialect when passed a block, maybe something like:
		//
		//     fail [<invalid-key> {The key} key-name: key {is invalid}]
		//
		// That could provide an error ID, the format message, and the
		// values to plug into the slots to make the message...which could
		// be extracted from the error if captured (e.g. error/id and
		// `error/key-name`.  Another option would be something like:
		//
		//     fail/with [{The key} :key-name {is invalid}] [key-name: key]
		//
		if (IS_BLOCK(reason)) {
			// Check to make sure we're only drawing from the limited types
			// we accept (reserving room for future dialect expansion)
			REBCNT index = VAL_INDEX(reason);
			for (; index < SERIES_LEN(VAL_SERIES(reason)); index++) {
				REBVAL *item = BLK_SKIP(VAL_SERIES(reason), index);
				if (IS_STRING(item) || IS_SCALAR(item) || IS_PAREN(item))
					continue;

				// We don't want to dispatch functions directly (use parens)

				// !!! This keeps the option open of being able to know that
				// strings that appear in the block appear in the error
				// message so it can be templated.

				if (IS_WORD(item)) {
					const REBVAL *var = TRY_GET_VAR(item);
					if (!var || !ANY_FUNC(var))
						continue;
				}

				// The only way to tell if a path resolves to a function
				// or not is to actually evaluate it, and we are delegating
				// to Reduce_Block ATM.  For now we force you to use a PAREN!
				//
				//     fail [{Erroring on} (the/safe/side) {for now.}]

				raise Error_0(RE_LIMITED_FAIL_INPUT);
			}

			// We just reduce and form the result, but since we allow PAREN!
			// it means you can put in pretty much any expression.
			Reduce_Block(reason, VAL_SERIES(reason), VAL_INDEX(reason), FALSE);
			Val_Init_String(reason, Copy_Form_Value(reason, 0));
		}

		if (!Make_Error_Object(D_OUT, reason)) {
			assert(THROWN(D_OUT));
			return R_OUT;
		}
		raise Error_Is(D_OUT);
	}

	DEAD_END;
}


/***********************************************************************
**
*/	REBNATIVE(if)
/*
***********************************************************************/
{
	if (IS_CONDITIONAL_FALSE(D_ARG(1))) return R_NONE;
	if (IS_BLOCK(D_ARG(2)) && !D_REF(3) /* not using /ONLY */) {
		if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(2)), 0))
			return R_OUT;

		return R_OUT;
	}
	return R_ARG2;
}


/***********************************************************************
**
*/	REBNATIVE(protect)
/*
***********************************************************************/
{
	REBCNT flags = FLAGIT(PROT_SET);

	if (D_REF(5)) SET_FLAG(flags, PROT_HIDE);
	else SET_FLAG(flags, PROT_WORD); // there is no unhide

	// accesses arguments 1 - 4
	return Protect(call_, flags);
}


/***********************************************************************
**
*/	REBNATIVE(unprotect)
/*
***********************************************************************/
{
	// accesses arguments 1 - 4
	return Protect(call_, FLAGIT(PROT_WORD));
}


/***********************************************************************
**
*/	REBNATIVE(reduce)
/*
***********************************************************************/
{
	if (IS_BLOCK(D_ARG(1))) {
		REBSER *ser = VAL_SERIES(D_ARG(1));
		REBCNT index = VAL_INDEX(D_ARG(1));
		REBOOL into = D_REF(5);

		if (into)
			*D_OUT = *D_ARG(6);

		if (D_REF(2))
			Reduce_Block_No_Set(D_OUT, ser, index, into);
		else if (D_REF(3))
			Reduce_Only(D_OUT, ser, index, D_ARG(4), into);
		else
			Reduce_Block(D_OUT, ser, index, into);

		return R_OUT;
	}

	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(return)
/*
**	The implementation of RETURN here is a simple THROWN() value and
**	has no "definitional scoping"--a temporary state of affairs.
**
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);

	Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_RETURN);
	CONVERT_NAME_TO_THROWN(D_OUT, arg);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(switch)
/*
**		value
**		cases [block!]
**		/default
**		case
**      /all {Check all cases}
**      /strict
**
***********************************************************************/
{
	REBVAL * const value = D_ARG(1);
	REBVAL * const cases = D_ARG(2);
	// has_default implied by default_case not being none
	REBVAL * const default_case = D_ARG(4);
	REBOOL all = D_REF(5);
	REBOOL strict = D_REF(6);

	REBOOL found = FALSE;

	REBVAL *item = VAL_BLK_DATA(cases);

	SET_NONE(D_OUT); // default return value if no cases run

	for (; NOT_END(item); item++) {

		// The way SWITCH works with blocks is that blocks are considered
		// bodies to match for other value types, so you can't use them
		// as case keys themselves.  They'll be skipped until we find
		// a non-block case we want to match.

		if (IS_BLOCK(item)) {
			// Each time we see a block that we don't take, we reset
			// the output to NONE!...because we only leak evaluations
			// out the bottom of the switch if no block would catch it

			SET_NONE(D_OUT);
			continue;
		}

		// GET-WORD!, GET-PATH!, and PAREN! are evaluated (an escaping
		// mechanism as in lit-quotes of function specs to avoid quoting)
		// You can still evaluate to one of these, e.g. `(quote :foo)` to
		// use parens to produce a GET-WORD! to test against.

		if (IS_PAREN(item)) {

		#if !defined(NDEBUG)
			if (LEGACY(OPTIONS_NO_SWITCH_EVALS)) {
				// !!! Note this as a delta in the legacy log
				*D_OUT = *item;
				goto compare_values;
			}
		#endif

			if (Do_Block_Throws(D_OUT, VAL_SERIES(item), VAL_INDEX(item)))
				return R_OUT;
		}
		else if (IS_GET_WORD(item)) {

		#if !defined(NDEBUG)
			if (LEGACY(OPTIONS_NO_SWITCH_EVALS)) {
				// !!! Note this as a delta in the legacy log
				*D_OUT = *item;
				goto compare_values;
			}
		#endif

			GET_VAR_INTO(D_OUT, item);
		}
		else if (IS_GET_PATH(item)) {
			const REBVAL *path = item;

		#if !defined(NDEBUG)
			if (LEGACY(OPTIONS_NO_SWITCH_EVALS)) {
				// !!! Note this as a delta in the legacy log
				*D_OUT = *item;
				goto compare_values;
			}
		#endif

			Do_Path(D_OUT, &path, NULL);
			if (THROWN(D_OUT))
				return R_OUT;
		}
		else {
			// Even if we're just using the item literally, we need to copy
			// it from the block the user loaned us...because the type
			// coercion in Compare_Modify_Values could mutate it.

			*D_OUT = *item;
		}

	#if !defined(NDEBUG)
	compare_values: // only used by LEGACY(OPTIONS_NO_SWITCH_EVALS)
	#endif

		// It's okay that we are letting the comparison change `value`
		// here, because equality is supposed to be transitive.  So if it
		// changes 0.01 to 1% in order to compare it, anything 0.01 would
		// have compared equal to so will 1%.  (That's the idea, anyway,
		// required for `a = b` and `b = c` to properly imply `a = c`.)

		if (!Compare_Modify_Values(value, D_OUT, strict ? 2 : 0))
			continue;

		// Skip ahead to try and find a block, to treat as code

		while (!IS_BLOCK(item)) {
			if (IS_END(item)) break;
			item++;
		}

		found = TRUE;

		if (Do_Block_Throws(D_OUT, VAL_SERIES(item), VAL_INDEX(item)))
			return R_OUT;

		// Only keep processing if the /ALL refinement was specified

		if (!all) return R_OUT;
	}

	if (!found && IS_BLOCK(default_case)) {
		if (Do_Block_Throws(
			D_OUT, VAL_SERIES(default_case), VAL_INDEX(default_case)
		)) {
			// No special handling needed if D_OUT is thrown, as we're
			// just going to return it anyway.
		}
		return R_OUT;
	}

	#if !defined(NDEBUG)
		// The previous answer to `switch 1 [1]` was a NONE!.  This was
		// a candidate for marking as an error, however the new idea is to
		// let cases that do not have a block after them be evaluated
		// (if necessary) and the last one to fall through and be the
		// result.  This offers a nicer syntax for a default, especially
		// when PAREN! is taken into account.
		//
		// However, running in legacy compatibility mode we need to squash
		// the value into a NONE! so it doesn't fall through.
		//
		if (LEGACY(OPTIONS_NO_SWITCH_FALLTHROUGH)) {
			if (!IS_NONE(D_OUT)) {
				// !!! Note this difference in legacy log
			}
			return R_NONE;
		}
	#endif

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(trap)
/*
**		1: block
**		2: /with
**		3: handler
**
***********************************************************************/
{
	REBVAL * const block = D_ARG(1);
	const REBFLG with = D_REF(2);
	REBVAL * const handler = D_ARG(3);

	REBOL_STATE state;
	const REBVAL *error;

	PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `raise Error` can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) {
		if (with) {
			if (IS_BLOCK(handler)) {
				// There's no way to pass 'error' to a block (so just DO it)
				if (Do_Block_Throws(
					D_OUT, VAL_SERIES(handler), VAL_INDEX(handler)
				)) {
					return R_OUT;
				}
				return R_OUT;
			}
			else if (ANY_FUNC(handler)) {
				if (
					(VAL_FUNC_NUM_PARAMS(handler) == 0)
					|| IS_REFINEMENT(VAL_FUNC_PARAM(handler, 1))
				) {
					// Arity zero handlers (or handlers whose first
					// parameter is a refinement) we call without the ERROR!
					//
					if (!Apply_Func_Throws(D_OUT, handler, NULL)) {
						// No need to handle thrown result specially, as we
						// are just returning it anyway
					}
				}
				else {
					// If the handler takes at least one parameter that
					// isn't a refinement, try passing it the ERROR! we
					// trapped.  Apply will do argument checking.
					//
					if (!Apply_Func_Throws(D_OUT, handler, error, NULL)) {
						// No need to handle thrown result specially, as we
						// are just returning it anyway
					}
				}

				return R_OUT;
			}

			panic Error_0(RE_MISC); // should not be possible (type-checking)
		}

		*D_OUT = *error;
		return R_OUT;
	}

	if (Do_Block_Throws(D_OUT, VAL_SERIES(block), VAL_INDEX(block))) {
		// Note that we are interested in when errors are raised, which
		// causes a tricky C longjmp() to the code above.  Yet a THROW
		// is different from that, and offers an opportunity to each
		// DO'ing stack level along the way to CATCH the thrown value
		// (with no need for something like the PUSH_TRAP above).
		//
		// We're being given that opportunity here, but doing nothing
		// and just returning the THROWN thing for other stack levels
		// to look at.  For the construct which does let you catch a
		// throw, see REBNATIVE(catch), which has code for this case.
	}

	DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(unless)
/*
***********************************************************************/
{
	if (IS_CONDITIONAL_TRUE(D_ARG(1))) return R_NONE;

	if (IS_BLOCK(D_ARG(2)) && !D_REF(3) /* not using /ONLY */) {
		if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(2)), 0))
			return R_OUT;

		return R_OUT;
	}

	return R_ARG2;
}
