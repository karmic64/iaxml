# iaxml

This is a tool that assists in batch downloading from the Internet Archive. While they do offer torrents for all of their collections, they unfortunately do not include all of the files in larger collections, so another solution was required...

Compilation requires a C compiler (preferably GCC), libiconv, openssl, and libxml2.

## Command-line usage

This program has 3 main modes of operation. You specify the command, then its parameters. We will be using the collection found [here](https://archive.org/details/tosec-main) for the following examples.

All of these commands require you to have downloaded the file ending in `_files.xml`. This contains all the information on the files contained in the collection.

### Downloading

`ixaml make xmlname archivename outname`

This command takes all the filenames found in the XML and turns them into a plaintext list of URLs that can then be downloaded with `wget`. `xmlname` should be the filename of the `_files.xml` you downloaded. `archivename` is the name of the collection. Usually, it is the part of the XML file name before `_files.xml`- in this case, `tosec-main`. `outname` is the name of the text file that will be output.

### Verifying

`iaxml verify xmlname dirname`

This command verifies the files you've downloaded against the SHA-1 hash contained in the XML file. `dirname` is the name of the directory containing all the files. After the command completes, it prints a report with the amount of files it attempted to verify, the amount of files that couldn't be verified, and the amount of files that failed verification.

### Sizing

`iaxml size xmlname`

This command is simple- it calculates and prints the total size of all the files in the XML file.

## Makefile usage

By default, simply running `make` will just build the tool. But there are some extra phony targets you can use to make the process of using the tool a bit more convenient.

Note that all of these commands require you to specify the value of two variables, `ARCHIVE_NAME` and `DEST_DIR`. `ARCHIVE_NAME` should be self-explanatory, but keep in mind that `DEST_DIR` is not the direct destination of the files- an extra subdirectory named after the archive name is made, and THIS is the true destination directory. The `_files.xml` file must also be located in the current working directory. Downloading requires `wget` to be installed.

* `ARCHIVE_NAME=tosec-main DEST_DIR=C:/roms make download` downloads all the files in `tosec-main_files.xml` to `C:/roms/tosec-main`.
* `ARCHIVE_NAME=tosec-main DEST_DIR=C:/roms make verify` verifies all the files in `C:/roms/tosec-main` using the SHA-1 hashes in `tosec-main_files.xml`.
* `ARCHIVE_NAME=tosec-main make size` prints the total size of all files in `tosec-main_files.xml`.