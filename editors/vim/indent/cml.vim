" Indentation settings for CommonML (.cml)
if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal expandtab
setlocal shiftwidth=2
setlocal softtabstop=2
setlocal tabstop=2
setlocal indentexpr=GetCommonMLIndent()
setlocal indentkeys+=0=else,0=\|

function! GetCommonMLIndent()
  let lnum = prevnonblank(v:lnum - 1)
  if lnum == 0
    return 0
  endif
  let prev = getline(lnum)
  let cur = getline(v:lnum)
  let ind = indent(lnum)
  if prev =~ '\%(=\|:\|with\|then\|else\)\s*$'
    let ind += shiftwidth()
  endif
  if cur =~ '^\s*else\>' && prev !~ '^\s*if\>'
    let ind -= shiftwidth()
  endif
  return ind < 0 ? 0 : ind
endfunction
