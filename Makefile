all : cenum

cenum : cenum.c

test : test.c test2.c
	$(CC) -E -P -DENUM_GEN $< | ./cenum -o lists '$(h)' '$(e)' '$(t)' $(s)
	$(CC) $< -o $@

h=-hconst struct {\
		 const char *key;\
		 int value;\
		 } $$(name)[] = {
e=-e{"$$(key)", $$(key)},
t=-t{0}};
s=-sEXPORT_TABLE
