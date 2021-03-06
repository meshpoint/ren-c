REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Generate auto headers"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Carl Sassenrath"
	Needs: 2.100.100
]

do %common.r

print "------ Building headers"

r3: system/version > 2.100.0

verbose: false
chk-dups: true
dups: make block! 10000 ; get pick [map! hash!] r3 1000
dup-found: false

do %form-header.r

tmp: context [

change-dir %../core/

count: 0
output: make string! 20000

emit: func [d] [append repend output d newline]
remit: func [d] [append repend rlib d newline]

emit-header: func [t f] [emit form-header/gen t f %make-headers]
rlib: form-header/gen "REBOL Interface Library" %reb-lib.h %make-headers.r
append rlib newline

append-spec: func [spec] [
	;?? spec
	assert [spec]
	if all [
		not find spec "static"
		not find spec "REBNATIVE("

		; The REBTYPE macro actually is expanded in the tmp-funcs
		; Should we allow macro expansion or do the REBTYPE another way?
		(comment [not find spec "REBTYPE("] true)

		find spec #"("
	][
		spec: trim spec
		either all [
			chk-dups
			find dups spec
		][
			print ["Duplicate:" the-file ":" spec]
			dup-found: true
		][
			append dups spec
		]
		either find spec "RL_API" [
			remit ["extern " spec "; // " the-file]
		][
			emit ["extern " spec "; // " the-file]
		]
		count: count + 1
	]
]

func-header: [
	;-- Scan for function header box:
	"^/**" to newline
	"^/*/" any [#" " | #"^-"]
	copy spec to newline (
		if find spec "()" [
			print [
				spec
				newline
				{C-Style void arguments should be foo(void) and not foo()}
				newline
				http://stackoverflow.com/questions/693788/c-void-arguments
			]
			fail "C++ no-arg prototype used instead of C style"
		]

		append-spec spec
	)
	newline
	[
		"/*" ; must be in func header section, not file banner
		any [
			thru "**"
			[#" " | #"^-"]
			copy line thru newline
		]
		thru "*/"
		|
		none
	]
]

process: func [file] [
	if verbose [?? file]
	data: read the-file: file
	if r3 [data: deline to-string data]
	parse data [
		any [
			thru "/******" to newline
			[
				func-header | thru newline
			]
		]
	]
]

emit-header "Function Prototypes" %funcs.h

emit {
#ifdef __cplusplus
extern "C" ^{
#endif
}

files: sort read %./

;do
[
	remove find files %a-lib2.c
	print "Non-extended reb-lib version"
	wait 5
]

for-each file files [
	if all [
		%.c = suffix? file
		not find/match file "host-"
		not find/match file "os-"
	][process file]
]

emit {
#ifdef __cplusplus
^}
#endif
}

write %../include/tmp-funcs.h output

print [count "function prototypes"]
;wait 1

;-------------------------------------------------------------------------

clear output

emit-header "Function Argument Enums" %func-args.h

make-arg-enums: func [word] [
	; Search file for definition:
	def: find acts to-set-word word
	def: skip def 2
	args: copy []
	refs: copy []
	; Gather arg words:
	for-each w first def [
		if any-word? w [
			append args uw: uppercase replace/all form to word! w #"-" #"_" ; R3
			if refinement? w [append refs uw  w: to word! w] ; R3
	 	]
	]

	uword: uppercase form word
	replace/all uword #"-" #"_"
	word: lowercase copy uword

	; Argument numbers:
	emit ["enum act_" word "_arg {"]
	emit [tab "ARG_" uword "_0,"]
	for-each w args [emit [tab "ARG_" uword "_" w ","]]
	emit [tab "ARG_" uword "_MAX"]
	emit "};^/"

	; Argument bitmask:
	n: 0
	emit ["enum act_" word "_mask {"]
	for-each w args [
		emit [tab "AM_" uword "_" w " = 1 << " n ","]
		n: n + 1
	]
	emit [tab "AM_" uword "_MAX"]
	emit "};^/"

	repend output ["#define ALL_" uword "_REFS ("]
	for-each w refs [
		repend output ["AM_" uword "_" w "|"]
	]
	remove back tail output
	append output ")^/^/"

	;?? output halt
]

acts: load %../boot/actions.r

for-each word [
	copy
	find
	select
	insert
	trim
	open
	read
	write
] [make-arg-enums word]

acts: load %../boot/natives.r

for-each word [
	checksum
	request-file
] [make-arg-enums word]

;?? output
write %../include/tmp-funcargs.h output


;-------------------------------------------------------------------------

clear output

emit-header "REBOL Constants Strings" %str-consts.h

data: to string! read %a-constants.c ;R3

parse data [
	some [
		to "^/const"
		copy d to "="
		(
			remove d
			;replace d "const" "extern"
			insert d "extern "
			append trim/tail d #";"
			emit d
		)
	]
]

write %../include/tmp-strings.h output

]

if any [dup-found verbose] [
	print "** NOTE ABOVE PROBLEM!"
	wait 5
]

print "   "
