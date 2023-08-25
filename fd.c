#include <dirent.h>
#include <assert.h>
#include <string.h>
#include <regex.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <grp.h>

const char *regex = ".*";

/* Buffer, containing current directory relative to startup CWD
 * with extra leading slash */
char buf[8 * 1024] = {0};
/* Current directory without leading slash */
char * const path = buf + 1;
/* Cur always points to slash */
char *cur = buf;

regex_t reg;

unsigned depth = UINT_MAX;

struct {
	uid_t    owner;
	gid_t    group;
	mode_t   mode;
	mode_t   modemask;
	unsigned ownermask    : 1;
	unsigned groupmask    : 1;
	unsigned ignorehidden : 1;
	unsigned ignorecase   : 1;
} opts = {0};

int isdelimiter(char c) {
	return c == '/' || c == '\0';
}

int isredundant(char *path) {
	return isdelimiter(path[0])
	    || path[0] == '.' && isdelimiter(path[1]);
}

void prettypath(void) {
	char *r = path, *w = path;
	char prev = '\0';
	while(*r) {
		if(prev == '/') {
			if(*r == '/') {
				++r;
				continue;
			}
		}
		if(prev == '/' || prev == '\0')
		if(r[0] == '.' && r[1] == '/') {
			r += 2;
			prev = '/';
			continue;
		}
		prev = *r;
		*(w++) = *(r++);
	}
	*w = '\0';
	cur = w - 1;
}

void pushd(const char *dir) {
	while((*(++cur) = *(dir++)));
	cur[0] = '/';
	cur[1] = '\0';
}

void popd(void) {
	while(*(--cur) != '/');
	cur[1] = '\0';
}

int mstat(struct stat st) {
	return (!opts.ownermask || st.st_uid == opts.owner)
	&&     (!opts.groupmask || st.st_gid == opts.group)
	&&     (st.st_mode & opts.modemask)  == opts.mode;
}

inline int ckstat(const char *path) {
	struct stat st;

	if(opts.modemask || opts.ownermask || opts.groupmask) {
		if(lstat(path, &st)) {
			perror(path);
			return 0;
		}
		return mstat(st);
	}
	return 1;
}

inline int matches(const char *fname) {
	return ckstat(fname)
	&& regexec(&reg, fname, 0, NULL, 0) == 0;
}

int searchdir(void) {
	DIR *dir = opendir(".");
	struct dirent *ent;

	if(!dir) return -1;
	while((ent = readdir(dir))) {
		if(ent->d_name[0] == '.'
		&& (opts.ignorehidden || ent->d_name[1] == '\0'
		|| ent->d_name[1] == '.' && ent->d_name[2] == '\0'))
			continue;
		if(matches(ent->d_name))
			printf("%s%s\n", path, ent->d_name);
		if(depth && ent->d_type == DT_DIR) {
			--depth;
			pushd(ent->d_name);
			if(chdir(ent->d_name) == 0)
				searchdir();
			chdir("..");
			popd();
			++depth;
		}
	}
	closedir(dir);
	return 0;
}

void setval(mode_t mask, mode_t val) {
	val &= mask;
	if(opts.modemask & mask && (opts.mode & mask) != val) {
		fprintf(stderr, "mutually exclusive file attributes\n");
		exit(EXIT_FAILURE);
	}
	opts.modemask |= mask;
	opts.mode &= ~mask;
	opts.mode |= val;
}

void setowner(const char *uname) {
	if(opts.ownermask) {
		fprintf(stderr, "only one user can be set\n");
		exit(EXIT_FAILURE);
	}
	struct passwd *pwd = getpwnam(uname);
	if(!pwd) {
		fprintf(stderr, "user \'%s\' does not exist\n", uname);
		exit(EXIT_FAILURE);
	}
	opts.ownermask = 1;
	opts.owner = pwd->pw_uid;
}

void setgroup(const char *group) {
	if(opts.groupmask) {
		fprintf(stderr, "only one group can be set\n");
		exit(EXIT_FAILURE);
	}
	struct group *grp = getgrnam(group);
	if(!grp) {
		fprintf(stderr, "group \'%s\' does not exist\n", group);
		exit(EXIT_FAILURE);
	}
	opts.groupmask = 1;
	opts.group = grp->gr_gid;
}

void usage(const char *progname) {
	printf("usage: %s [-defilpsxz] [-g group] [-u user] "
	       "[-r depth] [[regex] [directory]]\n",
	       progname);
	printf("  -d    match directory"    "\n"
	       "  -e    match executable"   "\n"
	       "  -f    match regular file" "\n"
		   "  -i    ignore case"        "\n"
	       "  -l    match link"         "\n"
	       "  -p    match pipe"         "\n"
	       "  -s    match socket"       "\n"
	       "  -x    match setuid"       "\n"
	       "  -z    ignore hidden"      "\n");
	exit(EXIT_SUCCESS);
}

void opterror(void) {
	fprintf(stderr, "unknown option '-%c'\n", optopt);
	exit(EXIT_FAILURE);
}

void getargs(int argc, char **argv) {
	int opt;

	opterr = 0;
	while((opt = getopt(argc, argv, "defg:hilpr:su:xz")) != -1) {
		switch(opt) {
			case 'd': setval(S_IFMT,  S_IFDIR);  break;
			case 'f': setval(S_IFMT,  S_IFREG);  break;
			case 'l': setval(S_IFMT,  S_IFLNK);  break;
			case 'p': setval(S_IFMT,  S_IFIFO);  break;
			case 's': setval(S_IFMT,  S_IFSOCK); break;
			case 'e': setval(S_IEXEC, S_IEXEC);  break;
			case 'x': setval(S_ISUID, S_ISUID);  break;
			case 'r': depth = atoi(optarg);      break;
			case 'u': setowner(optarg);          break;
			case 'g': setgroup(optarg);          break;
			case 'i': opts.ignorecase = 1;       break;
			case 'z': opts.ignorehidden = 1;     break;
			case 'h': usage(argv[0]);            break;
			case '?': opterror();                break;
			default:                             break;
		}
	}
	if(optind < argc) {
		regex = argv[optind++];
		if(optind < argc) {
			pushd(argv[optind++]);
			prettypath();
			if(optind < argc) {
				fprintf(stderr, "Too many positional arguments\n");
				exit(EXIT_FAILURE);
			}
		}
	}
}

int main(int argc, char **argv) {
	int error, cflags;

	setlocale(LC_ALL, ""); /* Let regex handle user encoding */
	*cur = '/';
	getargs(argc, argv);
	if(*path && chdir(path)) {
		perror(path);
		return 1;
	}
	cflags = REG_EXTENDED | REG_NOSUB | (opts.ignorecase ? REG_ICASE : 0);
	if((error = regcomp(&reg, regex, cflags))) {
		char errbuf[1024];
		regerror(error, &reg, errbuf, sizeof(errbuf));
		fprintf(stderr, "%s\n", errbuf);
		return 1;
	}
	searchdir();
	regfree(&reg);
	return 0;
}
