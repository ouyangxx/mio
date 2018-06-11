#ifndef __MIO_H__
#define __MIO_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#ifdef WIN32
#ifdef MIO_EXPORTS
#define MIO_API __declspec(dllexport)
#else
#define MIO_API __declspec(dllimport)
#endif
#else
#define MIO_API
#endif
#define PATH_LEN_MAX        (512)
typedef void * MFILE;

struct sfile
{
	char file_abs[PATH_LEN_MAX];//can be null
	char file_path[PATH_LEN_MAX];//path+name
	uint64_t file_size;
	uint32_t file_mode;
};

struct mfopen_context
{
	uint32_t op;//0,read 1,write
	uint32_t sfile_num;
	struct sfile *sfile_array;
	char path[PATH_LEN_MAX];
	char download_path[PATH_LEN_MAX];
};

MIO_API int64_t mfile_get_size(struct sfile *sfile_array, uint32_t sfile_num);
MIO_API MFILE *mfopen(const struct mfopen_context *context, const char *mode);
MIO_API int mfclose(MFILE *stream);
MIO_API int mfseek(MFILE *stream, long offset, int whence);
MIO_API int mfread(void *ptr, size_t size, size_t nmemb, MFILE *stream);
MIO_API size_t mfwrite(const void *ptr, size_t size, size_t nmeb, MFILE *stream);

#ifdef __cplusplus
}
#endif

#endif