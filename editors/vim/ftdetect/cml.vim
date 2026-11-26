" CommonML filetype detection for Vim and Neovim
augroup commonml_ftdetect
  autocmd!
  autocmd BufRead,BufNewFile *.cml,build.cml setfiletype commonml
  autocmd BufRead,BufNewFile *.cmlbc setfiletype commonmlbc
augroup END
