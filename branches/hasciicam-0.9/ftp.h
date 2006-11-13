extern int ftp_connected;
extern int ftp_debug;

void ftp_init(int passive);
void ftp_connect(char *host, char *user, char *pass, char *dir);
void ftp_upload(char *local, char *remote, char *tmp);
void ftp_close();
