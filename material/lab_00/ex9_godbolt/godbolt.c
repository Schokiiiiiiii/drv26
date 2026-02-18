int main() {

    volatile int i = 0;

    if (i) {
        return 1;
    }

    return 0;
}

// https://godbolt.org/