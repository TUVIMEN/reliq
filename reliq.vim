" Vim syntax file
" Language: reliq
" Maintainer: Dominik Stanisław Suchora <suchora.dominik7@gmail.com>
" Last Change: 2024-08-08

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

syn match rqHooks "\s[+-]\?\([maLlcC]\|match\|attributes\|level\|levelrelative\|children\|childmatch\|self\|child\|descendant\|desc\|ancestor\|parent\|rparent\|sibl\|spre\|ssub\|fsibl\|fspre\|fssub\|full\|relative_parent\|sibling\|sibling_preceding\|sibling_subsequent\|full_sibling\|full_sibling_preceding\|full_sibling_subsequent\)@"
syn match rqSpecialCharacter contained "\\."
syn region rqString start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=rqSpecialCharacter
syn region rqString start=+'+ skip=+\\\\\|\\'+ end=+'+ contains=rqSpecialCharacter
syn match rqNumber "-\?\d\+" contained
syn region rqRange start="\[" end="\]" contains=rqNumber
syn match rqSemicolon ";"
syn match rqComma ","

syn match rqField "\(^\|,\|{\)\s*\.[a-zA-Z0-9_-]\+" nextgroup=rqFieldType,rqFieldArray,rqChain

syn match rqFieldArray ".a[a-zA-Z0-9]*" nextgroup=rqFieldType,rqChain contained
syn match rqFieldArray ".a[a-zA-Z0-9]*(\"\(\\[0anrtbfv]\|.\)\")" nextgroup=rqFieldType,rqChain contained

syn match rqFieldType "\.[sbniu][a-zA-Z0-9]*" nextgroup=rqChain contained

syn region rqChain transparent start='\s' end=",\|$" contained contains=rqTag,rqHooks,rqString,rqRange,rqSemicolon,rqBlock,rqComma,rqFormat,rqField

syn region rqBlock start="{" end="}" contains=rqHooks,rqString,rqRange,rqSemicolon,rqField,rqBlock,rqFormat,rqComma
"syn region rqBlock start="{" end=/}\(\_s\+\ze\("\|{\)\)\@!/ transparent fold contains=rqHooks,rqString,rqRange,rqSemicolon,rqComma,rqField

syn region rqFormat start='\s[|/]\(\s\|$\)' end=',\|$' contained contains=rqString,rqRange,rqComma

hi def link rqField Function
hi def link rqFieldType rqField
hi def link rqFieldArray rqField
hi def link rqTag Type
hi def link rqChain Statement
hi def link rqHooks Special
hi def link rqString String
hi def link rqSpecialCharacter Special
hi def link rqNumber Number
hi def link rqRange Special
hi def link rqSemicolon Comment
hi def link rqComma Comment
hi def link rqFormat Keyword
"hi def link rqBlock Statement

let b:current_syntax = "reliq"
