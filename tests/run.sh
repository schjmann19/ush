echo Running test_if.sh
./ush < tests/test_if.sh > tests/test_if.out
if diff -u tests/test_if.expected tests/test_if.out; then echo test_if.sh passed; else echo test_if.sh failed; rm -f tests/test_if.out tests/test_for.out tests/test_case.out tests/test_functions.out; exit 1; fi

echo Running test_for.sh
./ush < tests/test_for.sh > tests/test_for.out
if diff -u tests/test_for.expected tests/test_for.out; then echo test_for.sh passed; else echo test_for.sh failed; rm -f tests/test_if.out tests/test_for.out tests/test_case.out tests/test_functions.out; exit 1; fi

echo Running test_case.sh
./ush < tests/test_case.sh > tests/test_case.out
if diff -u tests/test_case.expected tests/test_case.out; then echo test_case.sh passed; else echo test_case.sh failed; rm -f tests/test_if.out tests/test_for.out tests/test_case.out tests/test_functions.out tests/test_while_until.out; exit 1; fi

echo Running test_functions.sh
./ush < tests/test_functions.sh > tests/test_functions.out
if diff -u tests/test_functions.expected tests/test_functions.out; then echo test_functions.sh passed; else echo test_functions.sh failed; rm -f tests/test_if.out tests/test_for.out tests/test_case.out tests/test_functions.out tests/test_while_until.out; exit 1; fi

echo Running test_while_until.sh
./ush < tests/test_while_until.sh > tests/test_while_until.out
if diff -u tests/test_while_until.expected tests/test_while_until.out; then echo test_while_until.sh passed; else echo test_while_until.sh failed; rm -f tests/test_if.out tests/test_for.out tests/test_case.out tests/test_functions.out tests/test_while_until.out; exit 1; fi

rm -f tests/test_if.out tests/test_for.out tests/test_case.out tests/test_functions.out tests/test_while_until.out

echo All tests passed.
