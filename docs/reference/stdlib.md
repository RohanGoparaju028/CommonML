---
layout: default
title: Standard Library and Builtins Reference
---

# Standard Library & Builtins Reference

## Core & Diagnostics

- `print`, `println`, `to_string`, `not`, `abs`, `int_of_float`, `float_of_int`, `panic`, `cli_args`
- `heap_live_count ()` — returns current count of unclosed heap allocations (`new`)
- `ptr_is_alive p` — returns `bool` whether a pointer is alive
- `rc_count v`, `rc_live_objects ()` — reference-counting diagnostics

## String Builtins

- `++` (concatenation)
- `str_len`, `str_sub`, `str_byte_at`, `str_concat_list`, `str_index_of`, `str_split`, `str_trim`, `str_starts_with`, `str_ends_with`, `str_upper`, `str_lower`, `str_replace`

## Standard Modules

- `std/core` — `Option`, `Result`, `list_map`, `list_filter`, `list_foldl`, `list_any`, `list_all`, `list_find`, `range`, `compose`
- `std/math` — `pi`, `e`, `sin`, `cos`, `tan`, `atan`, `exp`, `log`, `pow`, `sqrt`, `floor`, `ceil`, `round`, `gcd`, `lcm`, `factorial`, `is_prime`, statistics & vectors
- `std/console` — terminal & formatted I/O helpers
- `std/net` & `std/http` — TCP sockets (`tcp_listen`, `tcp_connect`, `tcp_accept`, `sock_send`, `sock_recv`, `sock_close`) and HTTP/1.1 helpers

## File Operations

- `read_file`, `write_file`, `append_file`, `file_exists`, `delete_file`, `make_dir`, `is_dir`, `list_dir`, `file_size`, `copy_file`, `rename_file`
- Streaming handles: `file_open`, `file_read_line`, `file_read_all`, `file_write`, `file_close`
