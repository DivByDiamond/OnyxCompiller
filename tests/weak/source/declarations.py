"""File-scope attributes, declaration inheritance and invalid placements."""
from support_source import COMPILER


def cases(h):
    def check(name, source, symbols, version=2):
        h.inspect(h.compile(name, source), symbols, version)

    def syntax_only():
        src = h.write('syntax-only.c', b'int f(void) __attribute__((weak));')
        output = h.directory / 'syntax-only.onx'
        result = h.run([COMPILER, '-N',
                        '-fsyntax-only', src, '-o', output])
        assert result.returncode == 0, result.stderr
        assert not output.exists(), 'syntax-only emitted output'

    yield 'weak-syntax-only-accepted', syntax_only
    yield 'plain-stays-v1', lambda: check('plain', '''
        int f(void) { return 42; } int x = 42;
        static int hidden = 1; int *p = &hidden;
    ''', {'f': 0x15, 'x': 0x25}, 1)
    yield 'unused-weak-extern-stays-v1', lambda: check('unused-weak', '''
        extern int x __attribute__((weak));
        int f(void) __attribute__((weak));
        int plain(void) { return 42; }
    ''', {'plain': 0x15}, 1)
    for spelling in ('weak', '__weak__'):
        attr = f'__attribute__(({spelling}))'
        for position in ('prefix', 'postfix'):
            if position == 'prefix':
                source = f'{attr} int f(void) {{ return 42; }} {attr} int x = 42;'
            else:
                source = f'int f(void) {attr} {{ return 42; }} int x {attr} = 42;'
            name = spelling + '-' + position
            yield name, lambda n=name, s=source: check(n, s, {'f': 0x95, 'x': 0xa5})
    sources = {
        'unprototyped-weak-then-int-definition': '''
            int f() __attribute__((weak));
            int f(int x) { return x; }
        ''',
        'weak-array-completion': '''
            extern int a[] __attribute__((weak));
            extern int a[2];
            int (*array_ptr)[2] = &a;
        ''',
        'prototype-then-definition': '''
            __attribute__((weak)) int f(void);
            int f(void) { return 42; }
            extern int x __attribute__((weak)); int x = 42;
        ''',
        'late-weak-declaration': '''
            int f(void) { return 42; } int x = 42;
            int f(void) __attribute__((weak));
            extern int x __attribute__((weak));
        ''',
        'plain-redeclaration-inherits': '''
            int f(void) __attribute__((weak)); int f(void);
            extern int x __attribute__((weak)); extern int x;
            int (*fp)(void) = f; int *dp = &x;
        ''',
        'prefix-comma-vars': '''
            __attribute__((weak)) int x = 1, y = 2;
            int z = 3;
        ''',
        'declarator-prefix-comma-vars': '''
            int x = 1, __attribute__((weak)) y = 2, z = 3;
        ''',
        'declarator-prefix-comma-functions': '''
            int f(void), __attribute__((weak)) g(void), k(void);
            int (*fp)(void) = f; int (*gp)(void) = g;
            int (*kp)(void) = k;
        ''',
        'postfix-comma-vars': '''
            int x __attribute__((weak)) = 1, y = 2;
            int z = 3, w __attribute__((weak)) = 4;
        ''',
        'prefix-comma-functions': '''
            __attribute__((weak)) int f(void), g(void);
            int (*fp)(void) = f; int (*gp)(void) = g;
        ''',
        'postfix-comma-functions': '''
            int f(void) __attribute__((weak)), g(void);
            int (*fp)(void) = f; int (*gp)(void) = g;
        ''',
    }
    expected = {
        'unprototyped-weak-then-int-definition': {'f': 0x95},
        'weak-array-completion': {'a': 0xa6, 'array_ptr': 0x25},
        'prototype-then-definition': {'f': 0x95, 'x': 0xa5},
        'late-weak-declaration': {'f': 0x95, 'x': 0xa5},
        'plain-redeclaration-inherits': {'f': 0x96, 'x': 0xa6, 'fp': 0x25, 'dp': 0x25},
        'prefix-comma-vars': {'x': 0xa5, 'y': 0xa5, 'z': 0x25},
        'declarator-prefix-comma-vars': {'x': 0x25, 'y': 0xa5, 'z': 0x25},
        'declarator-prefix-comma-functions': {'f': 0x16, 'g': 0x96, 'k': 0x16},
        'postfix-comma-vars': {'x': 0xa5, 'y': 0x25, 'z': 0x25, 'w': 0xa5},
        'prefix-comma-functions': {'f': 0x96, 'g': 0x96},
        'postfix-comma-functions': {'f': 0x96, 'g': 0x16},
    }
    for name, source in sources.items():
        yield name, lambda n=name, s=source: check(n, s, expected[n])

    invalid = {
        'static-variable': 'static int x __attribute__((weak)) = 1;',
        'static-function': 'static __attribute__((weak)) int f(void) { return 1; }',
        'weak-prototype-static-definition': '''
            int f(void) __attribute__((weak));
            static int f(void) { return 1; }
        ''',
        'static-prototype-weak-definition': '''
            static int f(void);
            int f(void) __attribute__((weak)) { return 1; }
        ''',
        'weak-extern-static-variable': '''
            extern int x __attribute__((weak)); static int x = 1;
        ''',
        'static-variable-late-weak': '''
            static int x = 1; extern int x __attribute__((weak));
        ''',
        'variable-redeclared-weak-function': '''
            int x; int x(void) __attribute__((weak)) { return 1; }
        ''',
        'incompatible-weak-function-parameters': '''
            int f(int); __attribute__((weak)) int f(void);
        ''',
        'weak-void-prototype-then-int': '''
            int f(void) __attribute__((weak)); int f(int);
        ''',
        'unprototyped-weak-then-float': '''
            int f() __attribute__((weak)); int f(float);
        ''',
        'unprototyped-weak-then-char': '''
            int f() __attribute__((weak)); int f(char);
        ''',
        'unprototyped-weak-then-varargs': '''
            int f() __attribute__((weak)); int f(int, ...);
        ''',
        'weak-array-conflicting-completion': '''
            extern int a[] __attribute__((weak));
            extern int a[2]; extern int a[3];
            int *p = a;
        ''',
        'typedef': 'typedef int weak_int __attribute__((weak));',
        'local': 'int f(void) { int x __attribute__((weak)) = 1; return x; }',
        'local-extern': 'int f(void) { extern int x __attribute__((weak)); return x; }',
        'field': 'struct S { int x __attribute__((weak)); };',
        'parameter': 'int f(int x __attribute__((weak))) { return x; }',
        'weak-argument': 'int x __attribute__((weak(1))) = 1;',
        'weak-empty-arguments': 'int x __attribute__((weak())) = 1;',
        'alias-argument': '__attribute__((__weak__(1))) int f(void);',
    }
    for name, source in invalid.items():
        yield 'reject-' + name, lambda n=name, s=source: h.reject(n, s)
    for name, source in (
        ('direct-definition', '__attribute__((weak)) int x = 1;'),
        ('direct-prototype', 'int f(void) __attribute__((weak));'),
    ):
        yield name, lambda n=name, s=source: h.reject(
            n, s + '\nvoid _start(void) { for (;;) {} }', direct=True)
