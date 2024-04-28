#####################################################################
## C defines

ifdef COMSPEC
DOTEXE:=.exe
LIBICONV:=-liconv
else
DOTEXE:=
LIBICONV:=
endif

CFLAGS:=-flto -Ofast -Wall -Wextra -Wpedantic $(shell xml2-config --cflags)
LDFLAGS:=-s
LDLIBS:=-lcrypto $(shell xml2-config --libs)


%$(DOTEXE): %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)


####################################################################
## targets

.PHONY: default

default: iaxml$(DOTEXE)




####################################################################
## assisting rules

%.txt: %_files.xml iaxml$(DOTEXE)
	./iaxml make $< $* $@

.PHONY: download verify size
download: $(ARCHIVE_NAME).txt
	wget -i $< -c -N -x -nH --cut-dirs=1 --directory-prefix=$(DEST_DIR)

verify: $(ARCHIVE_NAME)_files.xml iaxml$(DOTEXE)
	./iaxml verify $< $(DEST_DIR)/$(ARCHIVE_NAME)

size: $(ARCHIVE_NAME)_files.xml iaxml$(DOTEXE)
	./iaxml size $<







