 extern int Test_hello_main(void); extern void Test_hello_tearDown(); extern void Test_hello_setUp(); extern int Test_assignment_validate_main(void); extern void Test_assignment_validate_tearDown(); extern void Test_assignment_validate_setUp();
void setUp(void) { Test_assignment_validate_setUp();  }
void tearDown(void) { Test_assignment_validate_tearDown();  }
int main(int argc, char **argv) { int rc=0;  rc=Test_hello_main(); if (rc != 0) { return rc; }  rc=Test_assignment_validate_main(); if (rc != 0) { return rc; }  }
