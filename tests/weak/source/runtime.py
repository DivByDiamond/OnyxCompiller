"""Link and execute only real compiler-produced source weak objects."""
from support_source import start


def cases(h):
    def override(order):
        root = h.compile('root', start('pick()+*dp+fp()', '''
            int pick(void); extern int value;
            int (*fp)(void) = pick; int *dp = &value;
        '''))
        strong = h.compile('strong', 'int pick(void) { return 14; } int value = 14;')
        weak = h.compile('fallback', '''
            __attribute__((weak)) int pick(void) { return 3; }
            int value __attribute__((__weak__)) = 3;
        ''')
        h.inspect(strong, {'pick': 0x15, 'value': 0x25}, 1)
        h.inspect(weak, {'pick': 0x95, 'value': 0xa5}, 2)
        objects = [weak, strong] if order == 'weak-first' else [strong, weak]
        h.execute([root, *objects])

    for order in ('weak-first', 'strong-first'):
        yield 'strong-over-weak-' + order, lambda o=order: override(o)

    def inherited(late):
        declaration = '''
            int pick(void) __attribute__((weak));
            extern int value __attribute__((weak));
        '''
        definition = 'int pick(void) { return 3; } int value = 3;'
        source = definition + declaration if late else declaration + definition
        weak = h.compile('inherited', source)
        h.inspect(weak, {'pick': 0x95, 'value': 0xa5}, 2)
        root = h.compile('inherit-root', start('pick()+value', '''
            int pick(void); extern int value;
        '''))
        strong = h.compile('inherit-strong', 'int pick(void) { return 20; } int value = 22;')
        h.execute([root, weak, strong])
        h.execute([root, strong, weak])

    yield 'prototype-weak-definition-runtime', lambda: inherited(False)
    yield 'late-weak-declaration-runtime', lambda: inherited(True)

    def missing(with_archive):
        root = h.compile('missing', start('42*(fp == 0 && dp == 0)', '''
            int absent(void) __attribute__((weak));
            extern int absent_data __attribute__((__weak__));
            int (*fp)(void) = absent; int *dp = &absent_data;
        '''))
        h.inspect(root, {'absent': 0x96, 'absent_data': 0xa6,
                         'fp': 0x25, 'dp': 0x25}, 2)
        inputs = [root]
        if with_archive:
            poison = h.compile('poison', '''
                int unavailable(void);
                int absent(void) { return unavailable(); } int absent_data = 7;
            ''')
            inputs.append(h.library('poison', poison))
        h.execute(inputs)

    def completed_array():
        root = h.compile('completed-array-root', start(
            '42*(sizeof(a) == 8 && a[0] == 20 && a[1] == 22)', '''
            extern int a[] __attribute__((weak));
            extern int a[2];
        '''))
        definition = h.compile('completed-array-definition', 'int a[2] = {20, 22};')
        h.inspect(root, {'a': 0xa6}, 2)
        h.inspect(definition, {'a': 0x25}, 1)
        h.execute([root, definition])

    yield 'weak-array-completion-sizeof-runtime', completed_array
    yield 'unresolved-weak-pointers-zero', lambda: missing(False)
    yield 'weak-undefined-does-not-pull-archive', lambda: missing(True)
