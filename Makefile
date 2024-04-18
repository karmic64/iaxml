#####################################################################
## C defines

ifdef COMSPEC
DOTEXE:=.exe
LIBICONV:=-liconv
INCDIR:=-I/mingw64/include/libxml2
else
DOTEXE:=
LIBICONV:=
INCDIR:=
endif

CFLAGS:=-flto -Ofast -Wall -Wextra -Wpedantic $(INCDIR)
LDFLAGS:=-s
LDLIBS:=-lxml2 -lcrypto $(LIBICONV)


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







