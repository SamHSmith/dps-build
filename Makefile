dps-build: src/*
	$(CC) $(CFLAGS) \
		-g -Werror -I. \
		-o $@ $< \
		$(LIBS)

clean:
	rm -f dps-build

.DEFAULT_GOAL=dps-build
.PHONY: clean
