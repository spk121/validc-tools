# Positive Test Cases: Backslash should escape these characters per POSIX
# Test 1: Escaping dollar sign ($)
if [ 'hello$' = "hello\$" ]; then
    echo "ok 1 - backslash escapes dollar sign"
else
    echo "not ok 1 - backslash escapes dollar sign"
fi

# Test 2: Escaping backtick (`)
if [ 'hello`' = "hello\`" ]; then
    echo "ok 2 - backslash escapes backtick"
else
    echo "not ok 2 - backslash escapes backtick"
fi

# Test 3: Escaping double quote (")
if [ 'hello"' = "hello\"" ]; then
    echo "ok 3 - backslash escapes double quote"
else
    echo "not ok 3 - backslash escapes double quote"
fi

# Test 4: Escaping backslash (\)
if [ 'hello\' = "hello\\" ]; then
    echo "ok 4 - backslash escapes backslash"
else
    echo "not ok 4 - backslash escapes backslash"
fi

# Test 5: Escaping newline (<newline> for line continuation)
expected='hello
world'
actual=$(echo "hello\
world")
if [ "$actual" = "$expected" ]; then
    echo "ok 5 - backslash escapes newline for line continuation"
else
    echo "not ok 5 - backslash escapes newline for line continuation"
fi

# Negative Test Cases: Backslash should not escape these characters
# Test 6: Backslash before 'a' (non-special character)
if [ 'hello\a' = "hello\\a" ]; then
    echo "ok 6 - backslash does not escape 'a'"
else
    echo "not ok 6 - backslash does not escape 'a'"
fi

# Test 7: Backslash before '!' (non-special character)
if [ 'hello\!' = "hello\\!" ]; then
    echo "ok 7 - backslash does not escape '!'"
else
    echo "not ok 7 - backslash does not escape '!'"
fi

# Test 8: Backslash before '#' (non-special character)
if [ 'hello\#' = "hello\\#" ]; then
    echo "ok 8 - backslash does not escape '#'"
else
    echo "not ok 8 - backslash does not escape '#'"
fi

# Test 9: Backslash before space (non-special in this context)
if [ 'hello\ ' = "hello\\ " ]; then
    echo "ok 9 - backslash does not escape space"
else
    echo "not ok 9 - backslash does not escape space"
fi

# Test 10: Backslash before '@' (non-special character)
if [ 'hello\@' = "hello\\@" ]; then
    echo "ok 10 - backslash does not escape '@'"
else
    echo "not ok 10 - backslash does not escape '@'"
fi
