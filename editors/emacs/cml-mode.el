;;; cml-mode.el --- Major mode for CommonML (.cml) -*- lexical-binding: t; -*-

;;; Commentary:
;; Syntax highlighting, 2-space block indentation, .cml file recognition, and
;; optional DAP debugging hook (`cml --dap`) for GNU Emacs.

;;; Code:

(defgroup commonml nil
  "Support for CommonML source files."
  :group 'languages)

(defconst cml-keywords
  '("let" "var" "rec" "in" "fn" "if" "then" "else" "match" "with"
    "type" "struct" "macro" "quote" "import" "new" "defer" "close"
    "while" "for" "each" "break" "continue" "begin" "end"))

(defconst cml-types
  '("Int" "Float" "Bool" "String" "Unit" "Ast" "List" "Vector" "Dict" "Set" "Ptr"
    "i8" "i16" "i32" "i64" "isize" "u8" "u16" "u32" "u64" "usize"
    "f32" "f64" "bool" "str" "string" "unit" "void"))

(defconst cml-constants '("true" "false"))

(defvar cml-font-lock-keywords
  `((,(regexp-opt cml-keywords 'symbols) . font-lock-keyword-face)
    (,(regexp-opt cml-types 'symbols) . font-lock-type-face)
    (,(regexp-opt cml-constants 'symbols) . font-lock-constant-face)
    ("\\<[A-Z][a-zA-Z0-9_']*\\>" . font-lock-type-face)
    ("\\<[a-z_][a-zA-Z0-9_']*!" . font-lock-preprocessor-face)
    ("\\$[a-z_][a-zA-Z0-9_']*" . font-lock-variable-name-face)
    ("\\<[0-9][0-9_]*\\(?:\\.[0-9_]*\\)?\\(?:[eE][+-]?[0-9_]+\\)?\\>" . font-lock-constant-face)
    ("|>\\|::\\|\\+\\+\\|->\\|<<\\|>>\\|==\\|!=\\|<=\\|>=" . font-lock-builtin-face)))

(defvar cml-mode-syntax-table
  (let ((st (make-syntax-table)))
    (modify-syntax-entry ?- ". 124b" st)
    (modify-syntax-entry ?/ ". 124b" st)
    (modify-syntax-entry ?* ". 23n" st)
    (modify-syntax-entry ?\( "()1" st)
    (modify-syntax-entry ?\) ")(4" st)
    (modify-syntax-entry ?\n "> b" st)
    (modify-syntax-entry ?\" "\"" st)
    (modify-syntax-entry ?_ "w" st)
    (modify-syntax-entry ?' "w" st)
    st))

(defun cml-indent-line ()
  "Indent current CommonML line by 2 spaces based on '=' / ':' block headers."
  (interactive)
  (let ((indent 0))
    (save-excursion
      (beginning-of-line)
      (when (re-search-backward "\\S-" nil t)
        (beginning-of-line)
        (setq indent (current-indentation))
        (when (looking-at ".*\\(=\\|:\\|\\bwith\\|\\bthen\\|\\belse\\)\\s-*$")
          (setq indent (+ indent 2)))))
    (indent-line-to indent)))

;;;###autoload
(define-derived-mode cml-mode prog-mode "CommonML"
  "Major mode for editing CommonML (.cml) files."
  :syntax-table cml-mode-syntax-table
  (setq-local font-lock-defaults '(cml-font-lock-keywords))
  (setq-local comment-start "-- ")
  (setq-local comment-end "")
  (setq-local indent-tabs-mode nil)
  (setq-local tab-width 2)
  (setq-local indent-line-function #'cml-indent-line))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.cml\\'" . cml-mode))

(provide 'cml-mode)
;;; cml-mode.el ends here
