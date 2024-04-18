#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>

#ifdef _WIN32
// GetACP()
#include <winnls.h>
#endif

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <openssl/evp.h>
#include <iconv.h>



//////////////////////////////////////////////////////////////
// xml iteration

typedef int (*ia_xml_file_func)(const xmlNodePtr node, void * custom);

int iterate_ia_xml_files(const char * xml_name, ia_xml_file_func func, void * custom) {
	int status = -1;
	
	xmlDocPtr doc = xmlParseFile(xml_name);
	if (!doc) {
		printf("error parsing %s\n", xml_name);
		goto iterate_ia_xml_files_cleanup;
	}
	
	xmlNodePtr root = xmlDocGetRootElement(doc);
	if (!root) {
		puts("document is empty");
		goto iterate_ia_xml_files_cleanup;
	}
	if (xmlStrcmp(root->name, (const xmlChar *)"files")) {
		printf("bad document, root element name is \"%s\", not \"files\"\n", root->name);
		goto iterate_ia_xml_files_cleanup;
	}
	
	xmlNodePtr cur = root->children;
	while (cur) {
		if (!xmlStrcmp(cur->name, (const xmlChar *)"file")) {
			func(cur, custom);
		}
		
		cur = cur->next;
	}
	status = 0;
	
iterate_ia_xml_files_cleanup:
	if (doc)
		xmlFreeDoc(doc);
	
	return status;
}





///////////////////////////////////////////////////////////////
// filename -> ia url conversion

#define FILENAME_BUF_SIZE 0x400

const char * make_ia_url(const char * archive_name, const char * filename) {
	static char buf[FILENAME_BUF_SIZE];
	sprintf(buf, "https://archive.org/download/%s/%s", archive_name, filename);
	return buf;
}

int fprint_ia_url(FILE * f, const char * archive_name, const char * filename) {
	return fprintf(f, "https://archive.org/download/%s/%s\n", archive_name, filename);
}



////////////////////////////////////////////////////////////////
// ia url list making

typedef struct {
	FILE * f;
	const char * archive_name;
} make_ia_url_t;


int make_ia_url_func(const xmlNodePtr node, void * custom) {
	make_ia_url_t * state = custom;
	
	xmlChar * filename = xmlGetProp(node, (const xmlChar*)"name");
	int status = filename ? fprint_ia_url(state->f, state->archive_name, (char *)filename) : -9;
	xmlFree(filename);
	return status;
}


int make_ia_url_list(const char * xml_name, const char * archive_name, const char * out_name) {
	int status = -1;
	
	FILE * f = fopen(out_name, "w");
	if (!f) {
		printf("can't open %s: %s\n", out_name, strerror(errno));
		goto make_ia_url_list_cleanup;
	}
	
	make_ia_url_t state = {
		f, archive_name
	};
	status = iterate_ia_xml_files(xml_name, make_ia_url_func, &state);
	
make_ia_url_list_cleanup:
	if (f)
		fclose(f);
	
	return status;
}




///////////////////////////////////////////////////////////////////
// file verification

#define VERIFY_BUF_SIZE 0x10000

typedef struct {
	const char * dir_name;
	size_t dir_name_len;
	unsigned verified_files;
	unsigned bad_files;
	unsigned failed_files;
	
#ifdef _WIN32
	iconv_t iconv;
#endif
	
	uint8_t * buf;
	
	const EVP_MD * sha1;
	EVP_MD_CTX * ctx;
	unsigned char * hash;
} verify_ia_files_t;


int verify_ia_files_func(const xmlNodePtr node, void * custom) {
	int status = -1;
	FILE * f = NULL;
	char * filename = NULL;
	char * correct_sha1 = NULL;
	
	verify_ia_files_t * state = custom;
	
	char fullname[FILENAME_BUF_SIZE];
	
	filename = (char *)xmlGetProp(node, (const xmlChar *)"name");
	if (!filename)
		goto verify_ia_files_func_cleanup;
	size_t filename_len = strlen(filename);
	
	// make a special exception for _files.xml, which never has hash
	if (filename_len >= sizeof("_files.xml")-1
		&& !memcmp(filename+filename_len-(sizeof("_files.xml")-1), "_files.xml", sizeof("_files.xml")-1)
	) {
		printf("Skipping unverifiable file %s\n", filename);
		goto verify_ia_files_func_cleanup;
	}
	
	state->verified_files++;
#ifdef _WIN32
	iconv(state->iconv, NULL, NULL, NULL, NULL);
	char * iconv_in = filename;
	char * iconv_out = &fullname[state->dir_name_len+1];
	size_t iconv_in_size = filename_len + 1; // include null too
	size_t iconv_out_size = FILENAME_BUF_SIZE - state->dir_name_len - 1;
	if (iconv(state->iconv, &iconv_in, &iconv_in_size, &iconv_out, &iconv_out_size) == (size_t)-1) {
		printf("Can't convert filename %s: %s\n", filename, strerror(errno));
		state->failed_files++;
		goto verify_ia_files_func_cleanup;
	}
	printf("Verifying %s...", &fullname[state->dir_name_len+1]);
#else
	printf("Verifying %s...", filename);
#endif
	
	xmlNodePtr sha1_node = node->children;
	while (sha1_node && xmlStrcmp(sha1_node->name, (const xmlChar *)"sha1")) {
		sha1_node = sha1_node->next;
	}
	if (!sha1_node) {
		puts("No SHA-1 node");
		state->failed_files++;
		goto verify_ia_files_func_cleanup;
	}
	correct_sha1 = (char *)xmlNodeGetContent(sha1_node);
	if (!correct_sha1) {
		puts("No SHA-1 hash");
		state->failed_files++;
		goto verify_ia_files_func_cleanup;
	}
	
	memcpy(fullname, state->dir_name, state->dir_name_len);
	fullname[state->dir_name_len] = '/';
#ifndef _WIN32
	strcpy(&fullname[state->dir_name_len+1], filename);
#endif
	
	f = fopen(fullname, "rb");
	if (!f) {
		puts(strerror(errno));
		state->failed_files++;
		goto verify_ia_files_func_cleanup;
	}
	
	if (!EVP_DigestInit_ex(state->ctx, state->sha1, NULL)) {
		puts("Can't initialize SHA-1 digest");
		state->failed_files++;
		goto verify_ia_files_func_cleanup;
	}
	while (1) {
		size_t read_bytes = fread(state->buf, 1, VERIFY_BUF_SIZE, f);
		if (ferror(f)) {
			puts(strerror(errno));
			state->failed_files++;
			goto verify_ia_files_func_cleanup;
		}
		
		if (!EVP_DigestUpdate(state->ctx, state->buf, read_bytes)) {
			puts("Can't update SHA-1 digest");
			state->failed_files++;
			goto verify_ia_files_func_cleanup;
		}
		
		if (feof(f))
			break;
	}
	if (!EVP_DigestFinal_ex(state->ctx, state->hash, NULL)) {
		puts("Can't finalize SHA-1 digest");
		state->failed_files++;
		goto verify_ia_files_func_cleanup;
	}
	
	int is_ok = 1;
	for (size_t i = 0; i < 20 * 2; i++) {
		const char hex_char_tbl[] = "0123456789abcdef";
		unsigned b = state->hash[i/2];
		if (i % 2)
			b &= 0x0f;
		else
			b >>= 4;
		char c = hex_char_tbl[b];
		putchar(c);
		
		if (c != correct_sha1[i])
			is_ok = 0;
	}
	
	if (is_ok) {
		puts("...OK");
	} else {
		printf("...BAD, expected %s\n", correct_sha1);
		state->bad_files++;
	}
	
verify_ia_files_func_cleanup:
	if (f)
		fclose(f);
	xmlFree(filename);
	xmlFree(correct_sha1);
	
	return status;
}


int verify_ia_files(const char * xml_name, const char * dir_name) {
	int status = -1;
	
#ifdef _WIN32
	char os_encoding[16];
	sprintf(os_encoding, "CP%u", GetACP());
#endif
	
	verify_ia_files_t state = {
		dir_name, strlen(dir_name), 0, 0, 0,
#ifdef _WIN32
		iconv_open(os_encoding, "UTF-8"),
#endif
		malloc(VERIFY_BUF_SIZE),
		EVP_sha1(), EVP_MD_CTX_new(), NULL
	};
#ifdef _WIN32
	if (state.iconv == (iconv_t)-1) {
		printf("couldn't open iconv from UTF-8 to %s\n", os_encoding);
		goto verify_ia_files_cleanup;
	}
#endif
	if (!state.ctx) {
		puts("couldn't create message digest context");
		goto verify_ia_files_cleanup;
	}
	state.hash = malloc(EVP_MD_get_size(state.sha1));
	status = iterate_ia_xml_files(xml_name, verify_ia_files_func, &state);
	
	printf("\n"
		"Verified %u files\n"
		"Couldn't verify %u files\n"
		"%u bad files\n",
		state.verified_files,
		state.failed_files,
		state.bad_files
	);
	
verify_ia_files_cleanup:
#ifdef _WIN32
	if (state.iconv != (iconv_t)-1)
		iconv_close(state.iconv);
#endif
	if (state.buf)
		free(state.buf);
	if (state.ctx)
		EVP_MD_CTX_free(state.ctx);
	if (state.hash)
		free(state.hash);
	
	return status;
}





////////////////////////////////////////////////////////////////////
// total size

typedef struct {
	unsigned files;
	unsigned failed_files;
	
	uintmax_t total_size;
} ia_files_total_size_t;


void print_size(uintmax_t bytes) {
	if (bytes >= (1ull << 40)) {
		printf("%f TiB", (double)bytes / (1ull << 40));
	} else if (bytes >= (1ull << 30)) {
		printf("%f GiB", (double)bytes / (1ull << 30));
	} else if (bytes >= (1ull << 20)) {
		printf("%f MiB", (double)bytes / (1ull << 20));
	} else if (bytes >= (1ull << 10)) {
		printf("%f KiB", (double)bytes / (1ull << 10));
	} else {
		printf("%u bytes", (unsigned)bytes);
	}
}


int get_ia_files_total_size_func(const xmlNodePtr node, void * custom) {
	int status = -1;
	char * filename = NULL;
	char * size_text = NULL;
	
	ia_files_total_size_t * state = custom;
	
	filename = (char *)xmlGetProp(node, (const xmlChar *)"name");
	if (!filename)
		goto get_ia_files_total_size_func_cleanup;
	size_t filename_len = strlen(filename);
	
	// make a special exception for _files.xml, which never has size
	if (filename_len >= sizeof("_files.xml")-1
		&& !memcmp(filename+filename_len-(sizeof("_files.xml")-1), "_files.xml", sizeof("_files.xml")-1)
	) {
		printf("Skipping uncheckable file %s\n", filename);
		goto get_ia_files_total_size_func_cleanup;
	}
	
	printf("Checking %s...", filename);
	
	state->files++;
	xmlNodePtr size_node = node->children;
	while (size_node && xmlStrcmp(size_node->name, (const xmlChar *)"size")) {
		size_node = size_node->next;
	}
	if (!size_node) {
		puts("No size node");
		state->failed_files++;
		goto get_ia_files_total_size_func_cleanup;
	}
	size_text = (char *)xmlNodeGetContent(size_node);
	if (!size_text) {
		puts("No size node content");
		state->failed_files++;
		goto get_ia_files_total_size_func_cleanup;
	}
	
	uintmax_t size = strtoumax(size_text, NULL, 0);
	print_size(size);
	putchar('\n');
	state->total_size += size;
	
	status = 0;
	
get_ia_files_total_size_func_cleanup:
	xmlFree(filename);
	xmlFree(size_text);
	
	return status;
}


int get_ia_files_total_size(const char * xml_name) {
	int status = -1;
	
	ia_files_total_size_t state = {
		0, 0, 0
	};
	status = iterate_ia_xml_files(xml_name, get_ia_files_total_size_func, &state);
	
	printf("\n"
		"Checked %u files\n"
		"Couldn't check %u files\n"
		"Total size: ",
		state.files,
		state.failed_files
	);
	print_size(state.total_size);
	putchar('\n');
	return status;
}







////////////////////////////////////////////////////////////////////
// main

void print_usage() {
	puts(
		"usage: iaxml command params..."
		"\n\tmake xmlname archivename outname"
		"\n\tverify xmlname dirname"
		"\n\tsize xmlname"
		);
}

int main(int argc, char * argv[]) {
	if (argc < 2) {
		print_usage();
		return EXIT_FAILURE;
	}
	const char * cmd = argv[1];
	
	if (!strcmp(cmd, "make")) {
		if (argc != 5) {
			puts("bad argument count");
			print_usage();
			return EXIT_FAILURE;
		}
		return make_ia_url_list(argv[2], argv[3], argv[4]);
	} else if (!strcmp(cmd, "verify")) {
		if (argc != 4) {
			puts("bad argument count");
			print_usage();
			return EXIT_FAILURE;
		}
		return verify_ia_files(argv[2], argv[3]);
	} else if (!strcmp(cmd, "size")) {
		if (argc != 3) {
			puts("bad argument count");
			print_usage();
			return EXIT_FAILURE;
		}
		return get_ia_files_total_size(argv[2]);
	}	else {
		puts("unrecognized command");
		print_usage();
		return EXIT_FAILURE;
	}
}





