" Vim / Neovim syntax highlighting for CommonML (.cml)
if exists("b:current_syntax")
  finish
endif

syntax keyword cmlKeyword let var rec in func type struct macro quote new defer close
syntax keyword cmlControl if then else match with while for each break continue import begin end
syntax keyword cmlBoolean true false
syntax keyword cmlPrimType Int Float Bool String Unit Ast List Vector Dict Set Ptr
syntax keyword cmlPrimType i8 i16 i32 i64 isize u8 u16 u32 u64 usize f32 f64 bool str string unit void

syntax match cmlUpperIdent "\<[A-Z][a-zA-Z0-9_']*\>"
syntax match cmlMacroCall "\<[a-z_][a-zA-Z0-9_']*!"
syntax match cmlUnquote "\$\([a-z_][a-zA-Z0-9_']*\|(\)\@="
syntax match cmlNumber "\<[0-9][0-9_]*\(\.[0-9_]*\)\=\([eE][+-]\=[0-9_]\+\)\=\>"
syntax match cmlOperator "|>\|::\|++\|->\|<<\|>>\|==\|!=\|<=\|>=\|&&\|||\|+\.\|-\.\|\*\.\|/\.\|[+\-*/%<>=|&^~!]"

syntax region cmlString start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=cmlEscape
syntax match cmlEscape contained "\\[ntr\\\"0]"

syntax match cmlLineComment "--.*$"
syntax match cmlSlashComment "//.*$"
syntax region cmlMLComment start="(\*" end="\*)" contains=cmlMLComment
syntax region cmlCComment start="/\*" end="\*/"

highlight default link cmlKeyword Keyword
highlight default link cmlControl Conditional
highlight default link cmlBoolean Boolean
highlight default link cmlPrimType Type
highlight default link cmlUpperIdent Type
highlight default link cmlMacroCall Macro
highlight default link cmlUnquote Special
highlight default link cmlNumber Number
highlight default link cmlOperator Operator
highlight default link cmlString String
highlight default link cmlEscape SpecialChar
highlight default link cmlLineComment Comment
highlight default link cmlSlashComment Comment
highlight default link cmlMLComment Comment
highlight default link cmlCComment Comment

let b:current_syntax = "commonml"
