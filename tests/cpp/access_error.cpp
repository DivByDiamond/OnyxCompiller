// Negative: a private data member touched from main() must be rejected.
// EXPECT-COMPILE-FAIL
class Secret {
    int hidden;
public:
    int shown;
};

int main() {
    Secret s;
    s.hidden = 1;
    return 0;
}
