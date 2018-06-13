#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mio.h"
#include "config.h"
#include "tlv.h"
#include "file.h"
#include "threadpoll.h"
#include "pthread.h"
#ifdef WIN32
#include <windows.h>
#include <winsock.h> 
#else
#include <unistd.h>
#include <arpa/inet.h>
#endif

#define DEFAULT_UNZIP_NUM	(10)
#define MAX_THREAD_NUM	(10)
#define MAX_TASK_SIZE	(20)
#define COM_MULTI_FILE_APPEND_VIRTUAL	(4)
#define SFILE_TAG_NUM_MAX		(3)
#define TLV_PAD(n) (((n) % 4) ? ((n) + (4 - ((n) % 4))) : (n))
#define FILE_MODE_DEFAULT   (00400 | 00200 | 00100 | 00040 | 00004) //0744
#define DIR_MODE_DEFAULT	(00400 | 00200 | 00100 | 00040 | 00010 | 00004 | 00001) //0755

struct mfile_session
{
	uint32_t file_type;
	uint32_t sfile_num;
	struct sfile *sfile_array;

	uint8_t *head_buf;
	uint64_t head_size;

	uint32_t op;//0,read 1,write
	char path[PATH_LEN_MAX];
	char download_path[PATH_LEN_MAX];
	FILE * fp;
	uint64_t cur_offset;
	uint64_t latest_write_len;

	struct unzip_param *uzip_param_array;
	uint32_t unzip_num;
	THREAD_POLL pThreadPoll;

	int is_open;
	pthread_mutex_t session_lock;
};

enum {
	TAG_SFILE_HEAD = 1,
	TAG_SFILE_PATH,
	TAG_SFILE_SIZE,
	TAG_SFILE_MODE,
};

struct unzip_param
{
	char src_path[PATH_LEN_MAX];
	uint64_t unzip_start_index;
	uint64_t unzip_len;
	char unzip_path[PATH_LEN_MAX];
};

struct thread_param
{
	struct mfile_session *pHandle;
	struct unzip_param unzip_param;
};

STATIC int is_open(struct mfile_session *pHandle)
{
	pthread_mutex_lock(&pHandle->session_lock);
	int is_open = pHandle->is_open;
	pthread_mutex_unlock(&pHandle->session_lock);
	return is_open;
}

void * thread_unzip(void *arg)
{
	if (NULL == arg)
	{
		return NULL;
	}
	struct thread_param *thread_param = (struct thread_param *)arg;
	if (NULL == thread_param)
	{
		return NULL;
	}
	if (file_access(thread_param->unzip_param.src_path) == -1)
	{
		return NULL;
	}
	if (file_access(thread_param->unzip_param.unzip_path) == -1)
	{
		file_touch(thread_param->unzip_param.unzip_path, FILE_MODE_DEFAULT);
	}
	FILE *fp_src = file_open(thread_param->unzip_param.src_path, "rb");
	FILE *fp_dest = file_open(thread_param->unzip_param.unzip_path, "rb+");
	if (NULL == fp_src)
	{
		return NULL;
	}
	if (NULL == fp_dest)
	{
		return NULL;
	}
    #ifdef WIN32
	int ret = fseeko64(fp_src, thread_param->unzip_param.unzip_start_index, SEEK_SET);
    #else
    int ret = fseeko(fp_src, thread_param->unzip_param.unzip_start_index, SEEK_SET);
    #endif
	if (ret < 0)
	{
		fclose(fp_src);
		fclose(fp_dest);
		return NULL;
	}
	int block_size = 992;
	char buf[1024] = { '\0' };
	int cnt = 0;
	uint64_t unzip_cnt = 0;
	while (is_open(thread_param->pHandle) && unzip_cnt < thread_param->unzip_param.unzip_len && (cnt = fread(buf, 1, block_size, fp_src)) > 0)
	{
		if (unzip_cnt + cnt > thread_param->unzip_param.unzip_len)
		{
			cnt = (int)(thread_param->unzip_param.unzip_len - unzip_cnt);
		}
		unzip_cnt += cnt;
		fwrite(buf, 1, cnt, fp_dest);
		memset(buf, 0, sizeof(buf));
	}
	fclose(fp_src);
	fclose(fp_dest);
	free(thread_param);
	thread_param = NULL;
	return NULL;
}

STATIC int is_already_unzip(struct mfile_session *pHandle, const char *unzip_path)
{
	if (NULL == pHandle)
	{
		return 0;
	}
	int i = 0;
	for (i = 0; i < pHandle->unzip_num; i++)
	{
		if (strcmp(pHandle->uzip_param_array[i].unzip_path, unzip_path) == 0)
		{
			return 1;
		}
	}
	return 0;
}

STATIC void append_unzip_param(struct mfile_session *pHandle, struct unzip_param param)
{
	if (NULL == pHandle)
	{
		return;
	}
	int i = 0;
	for (i = 0; i < pHandle->unzip_num; i++)
	{
		if (strcmp(pHandle->uzip_param_array[i].unzip_path, "") == 0)
		{
			memset(pHandle->uzip_param_array[i].src_path, 0, sizeof(pHandle->uzip_param_array[i].src_path));
			strncpy(pHandle->uzip_param_array[i].src_path, param.src_path, sizeof(param.src_path));
			pHandle->uzip_param_array[i].src_path[sizeof(pHandle->uzip_param_array[i].src_path) - 1] = '\0';

			memset(pHandle->uzip_param_array[i].unzip_path, 0, sizeof(pHandle->uzip_param_array[i].unzip_path));
			strncpy(pHandle->uzip_param_array[i].unzip_path, param.unzip_path, sizeof(param.unzip_path));
			pHandle->uzip_param_array[i].unzip_path[sizeof(pHandle->uzip_param_array[i].unzip_path) - 1] = '\0';

			pHandle->uzip_param_array[i].unzip_start_index = param.unzip_start_index;
			pHandle->uzip_param_array[i].unzip_len = param.unzip_len;
			return;
		}
	}
	uint32_t unzip_num = pHandle->unzip_num * 2;
	if (unzip_num > pHandle->sfile_num)
	{
		unzip_num = pHandle->sfile_num;
	}
	struct unzip_param *uzip_param_array = (struct unzip_param *)malloc(sizeof(struct unzip_param) * unzip_num);
	for (i = 0; i < unzip_num; i++)
	{
		memset(uzip_param_array[i].src_path, 0, sizeof(uzip_param_array[i].src_path));
		memset(uzip_param_array[i].unzip_path, 0, sizeof(uzip_param_array[i].unzip_path));
		uzip_param_array[i].unzip_start_index = 0;
		uzip_param_array[i].unzip_len = 0;

		if (i < pHandle->unzip_num)
		{
			strncpy(uzip_param_array[i].src_path, pHandle->uzip_param_array[i].src_path, sizeof(pHandle->uzip_param_array[i].src_path));
			uzip_param_array[i].src_path[sizeof(uzip_param_array[i].src_path) - 1] = '\0';

			strncpy(uzip_param_array[i].unzip_path, pHandle->uzip_param_array[i].unzip_path, sizeof(pHandle->uzip_param_array[i].unzip_path));
			uzip_param_array[i].unzip_path[sizeof(uzip_param_array[i].unzip_path) - 1] = '\0';

			uzip_param_array[i].unzip_start_index = pHandle->uzip_param_array[i].unzip_start_index;
			uzip_param_array[i].unzip_len = pHandle->uzip_param_array[i].unzip_len;
		}
		else if (i == pHandle->unzip_num)
		{
			strncpy(uzip_param_array[i].src_path, param.src_path, sizeof(pHandle->uzip_param_array[i].src_path));
			uzip_param_array[i].src_path[sizeof(uzip_param_array[i].src_path) - 1] = '\0';

			strncpy(uzip_param_array[i].unzip_path, param.unzip_path, sizeof(pHandle->uzip_param_array[i].unzip_path));
			uzip_param_array[i].unzip_path[sizeof(uzip_param_array[i].unzip_path) - 1] = '\0';

			uzip_param_array[i].unzip_start_index = param.unzip_start_index;
			uzip_param_array[i].unzip_len = param.unzip_len;
		}
	}
	free(pHandle->uzip_param_array);
	pHandle->uzip_param_array = uzip_param_array;
	pHandle->unzip_num = unzip_num;
}

STATIC void unzip_latest_sfile(struct mfile_session *pHandle)
{
	if (NULL == pHandle)
	{
		return;
	}
	if (pHandle->cur_offset == 0)
	{
		return;
	}
	int i = 0, j = 0;
	if (pHandle->head_size == 0)
	{
		FILE * fp = fopen(pHandle->path, "rb");
		if (NULL == fp)
		{
			return;
		}
		uint32_t file_type = 0;
		uint32_t sfile_num = 0;
		int cnt = 0;
		cnt = fread(&file_type, 1, sizeof(file_type), fp);
		if (cnt < sizeof(file_type))
		{
			fclose(fp);
			return;
		}
		file_type = ntohs(file_type);
		cnt = fread(&sfile_num, 1, sizeof(sfile_num), fp);
		if (cnt < sizeof(sfile_num))
		{
			fclose(fp);
			return;
		}
		sfile_num = ntohs(sfile_num);
		if (sfile_num == 0)
		{
			fclose(fp);
			return;
		}
		struct sfile *sfile_array = (struct sfile *)malloc(sizeof(struct sfile) * sfile_num);
		if (NULL == sfile_array)
		{
			fclose(fp);
			return;
		}
		for (i = 0; i < sfile_num; i++)
		{
			uint16_t sfile_head_type = 0;
			uint16_t sfile_head_len = 0;
			cnt = fread(&sfile_head_type, 1, sizeof(sfile_head_type), fp);
			if (cnt < sizeof(sfile_head_type))
			{
				free(sfile_array);
				fclose(fp);
				return;
			}
			sfile_head_type = ntohs(sfile_head_type);
			if (sfile_head_type != TAG_SFILE_HEAD)
			{
				free(sfile_array);
				fclose(fp);
				return;
			}
			cnt = fread(&sfile_head_len, 1, sizeof(sfile_head_len), fp);
			if (cnt < sizeof(sfile_head_len))
			{
				free(sfile_array);
				fclose(fp);
				return;
			}
			sfile_head_len = ntohs(sfile_head_len);
			uint8_t *buf = (uint8_t *)malloc(sizeof(uint8_t) * sfile_head_len);
			if (NULL == buf)
			{
				free(sfile_array);
				fclose(fp);
				return;
			}
			cnt = fread(buf, 1, sfile_head_len, fp);
			if (cnt < sfile_head_len)
			{
				free(buf);
				free(sfile_array);
				fclose(fp);
				return;
			}
			int iter = 0;
			struct tlv tlv;
			tlv_init(&tlv);
			tlv_raw_parse(&tlv, buf, sfile_head_len, iter);
			if (tlv.type != TAG_SFILE_PATH)
			{
				free(buf);
				free(sfile_array);
				fclose(fp);
				return;
			}
			memset(sfile_array[i].file_abs, 0, sizeof(sfile_array[i].file_abs));
			memset(sfile_array[i].file_path, 0, sizeof(sfile_array[i].file_path));
			strncpy(sfile_array[i].file_path, tlv.value, tlv.len);
			iter += tlv.total_len;

			tlv_init(&tlv);
			tlv_raw_parse(&tlv, buf, sfile_head_len, iter);
			if (tlv.type != TAG_SFILE_SIZE)
			{
				free(buf);
				free(sfile_array);
				fclose(fp);
				return;
			}
			sfile_array[i].file_size = *((uint16_t *)tlv.value);
			iter += tlv.total_len;

			tlv_init(&tlv);
			tlv_raw_parse(&tlv, buf, sfile_head_len, iter);
			if (tlv.type != TAG_SFILE_MODE)
			{
				free(buf);
				free(sfile_array);
				fclose(fp);
				return;
			}
			sfile_array[i].file_mode = *((uint16_t *)tlv.value);
			iter += tlv.total_len;

			free(buf);
		}
#ifdef WIN32
		pHandle->head_size = ftello64(fp);
#else
		pHandle->head_size = ftello(fp);
#endif
		fclose(fp);
		pHandle->file_type = file_type;
		pHandle->sfile_num = sfile_num;
		pHandle->sfile_array = sfile_array;

		pHandle->unzip_num = DEFAULT_UNZIP_NUM;
		if (pHandle->unzip_num > pHandle->sfile_num)
		{
			pHandle->unzip_num = pHandle->sfile_num;
		}
		pHandle->uzip_param_array = (struct unzip_param *)malloc(sizeof(struct unzip_param) * pHandle->unzip_num);
		if (NULL == pHandle->uzip_param_array)
		{
			return;
		}
		for (i = 0; i < pHandle->unzip_num; i++)
		{
			memset(pHandle->uzip_param_array[i].src_path, 0, sizeof(pHandle->uzip_param_array[i].src_path));
			memset(pHandle->uzip_param_array[i].unzip_path, 0, sizeof(pHandle->uzip_param_array[i].unzip_path));
			pHandle->uzip_param_array[i].unzip_start_index = 0;
			pHandle->uzip_param_array[i].unzip_len = 0;
		}
	}
	uint64_t latest_write_start = pHandle->cur_offset - pHandle->latest_write_len;
	uint64_t latest_write_end = pHandle->cur_offset;
	uint64_t tmp_size = pHandle->head_size;
	for (i = 0; i < pHandle->sfile_num; i++)
	{
		tmp_size += pHandle->sfile_array[i].file_size;
		if (latest_write_start < tmp_size)
		{
			tmp_size -= pHandle->sfile_array[i].file_size;
			for (j = i; j < pHandle->sfile_num; j++)
			{
				tmp_size += pHandle->sfile_array[j].file_size;
				if (latest_write_end >= tmp_size)
				{
					//pHandle->sfile_array[j].file_path
					char download_path[PATH_LEN_MAX];
					path_removeSuffix(pHandle->download_path, download_path, sizeof(download_path));
					char file_path[PATH_LEN_MAX];
					path_addPrefix(pHandle->sfile_array[j].file_path, file_path, sizeof(file_path));
					char unzip_path[PATH_LEN_MAX];
					memset(unzip_path, 0, sizeof(unzip_path));
					sprintf(unzip_path, "%s%s", download_path, file_path);
					if (is_already_unzip(pHandle, unzip_path))
					{
						continue;
					}
					int64_t fileSize = 0;
					if (file_access(unzip_path) == -1 || (file_size(unzip_path, &fileSize) == 0 && fileSize != pHandle->sfile_array[j].file_size))
					{
						//thread unzip
						struct unzip_param unzip_param;
						memset(unzip_param.src_path, 0, sizeof(unzip_param.src_path));
						strncpy(unzip_param.src_path, pHandle->path, sizeof(pHandle->path));
						unzip_param.src_path[sizeof(unzip_param.src_path) - 1] = '\0';

						memset(unzip_param.unzip_path, 0, sizeof(unzip_param.unzip_path));
						strncpy(unzip_param.unzip_path, unzip_path, sizeof(unzip_path));
						unzip_param.unzip_path[sizeof(unzip_param.unzip_path) - 1] = '\0';

						unzip_param.unzip_start_index = tmp_size - pHandle->sfile_array[j].file_size;
						unzip_param.unzip_len = pHandle->sfile_array[j].file_size;

						append_unzip_param(pHandle, unzip_param);

						struct thread_param *thread_param = (struct thread_param *)malloc(sizeof(struct thread_param));
						thread_param->pHandle = pHandle;
						thread_param->unzip_param = unzip_param;
						threadpoll_add_task(pHandle->pThreadPoll, thread_unzip, (void *)thread_param);
					}
				}
				else
				{
					break;
				}
			}
			break;
		}
	}
}

STATIC void count_head_size(struct mfile_session *pHandle)
{
	if (NULL == pHandle)
	{
		return;
	}
	int i = 0;
	pHandle->head_size = 0;
	pHandle->head_size += sizeof(pHandle->file_type);
	pHandle->head_size += sizeof(pHandle->sfile_num);
	for (i = 0; i < pHandle->sfile_num; i++)
	{
		pHandle->head_size += sizeof(uint16_t);
		pHandle->head_size += sizeof(uint16_t);

		pHandle->head_size += sizeof(uint16_t);
		pHandle->head_size += sizeof(uint16_t);
		pHandle->head_size += TLV_PAD(strlen(pHandle->sfile_array[i].file_path));

		pHandle->head_size += sizeof(uint16_t);
		pHandle->head_size += sizeof(uint16_t);
		pHandle->head_size += TLV_PAD(sizeof(pHandle->sfile_array[i].file_size));

		pHandle->head_size += sizeof(uint16_t);
		pHandle->head_size += sizeof(uint16_t);
		pHandle->head_size += TLV_PAD(sizeof(pHandle->sfile_array[i].file_mode));
	}
}

STATIC void load_head(struct mfile_session *pHandle)
{
	if (NULL == pHandle)
	{
		return;
	}
	int i = 0;
	count_head_size(pHandle);
	pHandle->head_buf = (uint8_t *)malloc((size_t)(sizeof(uint8_t)* pHandle->head_size));
	if (NULL == pHandle->head_buf)
	{
		return;
	}
	uint64_t remain_buf_len = pHandle->head_size;
	int iter = 0;
	*(uint32_t *)(pHandle->head_buf + iter) = htons(pHandle->file_type); iter += sizeof(uint32_t); remain_buf_len -= sizeof(uint32_t); 
	*(uint32_t *)(pHandle->head_buf + iter) = htons(pHandle->sfile_num); iter += sizeof(uint32_t); remain_buf_len -= sizeof(uint32_t);
	for (i = 0; i < pHandle->sfile_num; i++)
	{
		uint16_t sfile_head_type = TAG_SFILE_HEAD;
		uint16_t sfile_head_len = 0;
		sfile_head_len += sizeof(uint16_t);
		sfile_head_len += sizeof(uint16_t);
		sfile_head_len += TLV_PAD(strlen(pHandle->sfile_array[i].file_path));
		sfile_head_len += sizeof(uint16_t);
		sfile_head_len += sizeof(uint16_t);
		sfile_head_len += TLV_PAD(sizeof(pHandle->sfile_array[i].file_size));
		sfile_head_len += sizeof(uint16_t);
		sfile_head_len += sizeof(uint16_t);
		sfile_head_len += TLV_PAD(sizeof(pHandle->sfile_array[i].file_mode));

		*(uint16_t *)(pHandle->head_buf + iter) = htons(sfile_head_type); iter += sizeof(uint16_t); remain_buf_len -= sizeof(uint16_t);
		*(uint16_t *)(pHandle->head_buf + iter) = htons(sfile_head_len); iter += sizeof(uint16_t); remain_buf_len -= sizeof(uint16_t);
		int total_len = tlv_raw_append(pHandle->head_buf, (int)remain_buf_len, (uint16_t)TAG_SFILE_PATH, (void *)pHandle->sfile_array[i].file_path, (uint16_t)strlen(pHandle->sfile_array[i].file_path), iter); iter += total_len; remain_buf_len -= total_len;
		total_len = tlv_raw_append(pHandle->head_buf, (int)remain_buf_len, (uint16_t)TAG_SFILE_SIZE, (void *)&pHandle->sfile_array[i].file_size, (uint16_t)sizeof(pHandle->sfile_array[i].file_size), iter); iter += total_len; remain_buf_len -= total_len;
		total_len = tlv_raw_append(pHandle->head_buf, (int)remain_buf_len, (uint16_t)TAG_SFILE_MODE, (void *)&pHandle->sfile_array[i].file_mode, (uint16_t)sizeof(pHandle->sfile_array[i].file_mode), iter); iter += total_len; remain_buf_len -= total_len;
	}
}

int64_t mfile_get_size(struct sfile *sfile_array, uint32_t sfile_num)
{
	if (NULL == sfile_array)
	{
		return 0;
	}
	int64_t countSize = 0;
	int i = 0;
	countSize += sizeof(uint32_t);
	countSize += sizeof(uint32_t);
	for (i = 0; i < sfile_num; i++)
	{
		countSize += sizeof(uint16_t);
		countSize += sizeof(uint16_t);

		countSize += sizeof(uint16_t);
		countSize += sizeof(uint16_t);
		countSize += TLV_PAD(strlen(sfile_array[i].file_path));

		countSize += sizeof(uint16_t);
		countSize += sizeof(uint16_t);
		countSize += TLV_PAD(sizeof(sfile_array[i].file_size));

		countSize += sizeof(uint16_t);
		countSize += sizeof(uint16_t);
		countSize += TLV_PAD(sizeof(sfile_array[i].file_mode));

		countSize += sfile_array[i].file_size;
	}
	return countSize;
}

int mfile_unzip(const char *src_file, const char *dest_path)
{
	int ret = -1;
	int i = 0;
	int cnt = 0;
	uint32_t file_type = 0;
	uint32_t sfile_num = 0;
	struct sfile *sfile_array = NULL;
	//int64_t fsize = 0;
	//int64_t src_fsize = 0;
	//file_size(src_file, &src_fsize);

	FILE * fp_src = fopen(src_file, "rb");
	if (NULL == fp_src)
	{
		return ret;
	}
	cnt = fread(&file_type, 1, sizeof(file_type), fp_src);
	if (cnt < sizeof(file_type))
	{
		fclose(fp_src);
		return ret;
	}
	file_type = ntohs(file_type);
	cnt = fread(&sfile_num, 1, sizeof(sfile_num), fp_src);
	if (cnt < sizeof(sfile_num))
	{
		fclose(fp_src);
		return ret;
	}
	sfile_num = ntohs(sfile_num);
	if (sfile_num == 0)
	{
		fclose(fp_src);
		return ret;
	}
	sfile_array = (struct sfile *)malloc(sizeof(struct sfile) * sfile_num);
	if (NULL == sfile_array)
	{
		fclose(fp_src);
		return ret;
	}
	for (i = 0; i < sfile_num; i++)
	{
		uint16_t sfile_head_type = 0;
		uint16_t sfile_head_len = 0;
		cnt = fread(&sfile_head_type, 1, sizeof(sfile_head_type), fp_src);
		if (cnt < sizeof(sfile_head_type))
		{
			free(sfile_array);
			fclose(fp_src);
			return ret;
		}
		sfile_head_type = ntohs(sfile_head_type);
		if (sfile_head_type != TAG_SFILE_HEAD)
		{
			free(sfile_array);
			fclose(fp_src);
			return ret;
		}
		cnt = fread(&sfile_head_len, 1, sizeof(sfile_head_len), fp_src);
		if (cnt < sizeof(sfile_head_len))
		{
			free(sfile_array);
			fclose(fp_src);
			return ret;
		}
		sfile_head_len = ntohs(sfile_head_len);
		uint8_t *buf = (uint8_t *)malloc(sizeof(uint8_t)* sfile_head_len);
		if (NULL == buf)
		{
			free(sfile_array);
			fclose(fp_src);
			return ret;
		}
		cnt = fread(buf, 1, sfile_head_len, fp_src);
		if (cnt < sfile_head_len)
		{
			free(buf);
			free(sfile_array);
			fclose(fp_src);
			return ret;
		}
		int iter = 0;
		struct tlv tlv;
		tlv_init(&tlv);
		tlv_raw_parse(&tlv, buf, sfile_head_len, iter);
		if (tlv.type != TAG_SFILE_PATH)
		{
			free(buf);
			free(sfile_array);
			fclose(fp_src);
			return ret;
		}
		memset(sfile_array[i].file_abs, 0, sizeof(sfile_array[i].file_abs));
		memset(sfile_array[i].file_path, 0, sizeof(sfile_array[i].file_path));
		strncpy(sfile_array[i].file_path, tlv.value, tlv.len);
		iter += tlv.total_len;

		tlv_init(&tlv);
		tlv_raw_parse(&tlv, buf, sfile_head_len, iter);
		if (tlv.type != TAG_SFILE_SIZE)
		{
			free(buf);
			free(sfile_array);
			fclose(fp_src);
			return ret;
		}
		sfile_array[i].file_size = *((uint16_t *)tlv.value);
		iter += tlv.total_len;

		tlv_init(&tlv);
		tlv_raw_parse(&tlv, buf, sfile_head_len, iter);
		if (tlv.type != TAG_SFILE_MODE)
		{
			free(buf);
			free(sfile_array);
			fclose(fp_src);
			return ret;
		}
		sfile_array[i].file_mode = *((uint16_t *)tlv.value);
		iter += tlv.total_len;

		free(buf);
	}
	//fsize = mfile_get_size(sfile_array, sfile_num);

	ret = 0;
	for (i = 0; i < sfile_num; i++)
	{
		if (ret != 0)
		{
			break;
		}
		char download_path[PATH_LEN_MAX];
		path_removeSuffix(dest_path, download_path, sizeof(download_path));
		char f_path[PATH_LEN_MAX];
		path_addPrefix(sfile_array[i].file_path, f_path, sizeof(f_path));
		char unzip_file[PATH_LEN_MAX];
		memset(unzip_file, 0, sizeof(unzip_file));
		sprintf(unzip_file, "%s%s", download_path, f_path);
		int64_t fileSize = 0;
		if (file_access(unzip_file) == -1 || (file_size(unzip_file, &fileSize) == 0 && fileSize != sfile_array[i].file_size))
		{
			//unzip
			char unzip_path[PATH_LEN_MAX];
			file_path(unzip_file, unzip_path, sizeof(unzip_path));
			if (folder_access(unzip_path) == -1)
			{
				folder_touch(unzip_path, DIR_MODE_DEFAULT);
			}
			if (file_access(unzip_file) == -1)
			{
				file_touch(unzip_file, FILE_MODE_DEFAULT);
			}
			FILE *fp_dest = file_open(unzip_file, "rb+");
			if (NULL == fp_dest)
			{
				ret = -1;
				break;
			}
			int block_size = 992;
			char buf[1024] = { '\0' };
			int cnt = 0;
			uint64_t unzip_cnt = 0;
			while (unzip_cnt < sfile_array[i].file_size)
			{
				if (unzip_cnt + block_size < sfile_array[i].file_size)
				{
					cnt = fread(buf, 1, block_size, fp_src);
				}
				else
				{
					cnt = fread(buf, 1, sfile_array[i].file_size - unzip_cnt, fp_src);
				}
				if (cnt <= 0)
				{
					ret = -1;
					break;
				}
				unzip_cnt += cnt;
				fwrite(buf, 1, cnt, fp_dest);
				memset(buf, 0, sizeof(buf));
			}
			fclose(fp_dest);
		}
		else
		{
			#ifdef WIN32
			ret = fseeko64(fp_src, sfile_array[i].file_size, SEEK_CUR);
			#else
			ret = fseeko(fp_src, sfile_array[i].file_size, SEEK_CUR);
			#endif
		}
	}
	free(sfile_array);
	fclose(fp_src);

	//if (src_fsize == fsize)
	//{
	//	file_remove(src_file);
	//}
	return ret;
}

MFILE *mfopen(const struct mfopen_context *context, const char *mode)
{
	if (NULL == context || NULL == mode)
	{
		return NULL;
	}
	struct mfile_session *pHandle = (struct mfile_session *)malloc(sizeof(struct mfile_session));
	if (NULL == pHandle)
	{
		return NULL;
	}
	pHandle->file_type = COM_MULTI_FILE_APPEND_VIRTUAL;
	pHandle->sfile_num = context->sfile_num;
	pHandle->sfile_array = NULL;
	pHandle->head_buf = NULL;
	pHandle->head_size = 0;
	pHandle->op = context->op;
	strncpy(pHandle->path, context->path, PATH_LEN_MAX);
	pHandle->path[PATH_LEN_MAX - 1] = '\0';
	strncpy(pHandle->download_path, context->download_path, PATH_LEN_MAX);
	pHandle->download_path[PATH_LEN_MAX - 1] = '\0';
	pHandle->fp = NULL;
	pHandle->cur_offset = 0;
	pHandle->latest_write_len = 0;
	pHandle->uzip_param_array = NULL;
	pHandle->unzip_num = 0;
	pHandle->pThreadPoll = NULL;
	pHandle->is_open = 1;
	pthread_mutex_init(&pHandle->session_lock, NULL);
	int i = 0;
	if (pHandle->op == 0)//read
	{
		pHandle->sfile_array = (struct sfile *)malloc(sizeof(struct sfile) * pHandle->sfile_num);
		if (NULL == pHandle->sfile_array)
		{
			return NULL;
		}
		for (i = 0; i < context->sfile_num; i++)
		{
			strncpy(pHandle->sfile_array[i].file_abs, context->sfile_array[i].file_abs, PATH_LEN_MAX);
			pHandle->sfile_array[i].file_abs[PATH_LEN_MAX - 1] = '\0';
			strncpy(pHandle->sfile_array[i].file_path, context->sfile_array[i].file_path, PATH_LEN_MAX);
			pHandle->sfile_array[i].file_path[PATH_LEN_MAX - 1] = '\0';
			pHandle->sfile_array[i].file_size = context->sfile_array[i].file_size;
			pHandle->sfile_array[i].file_mode = context->sfile_array[i].file_mode;
		}
		load_head(pHandle);
	}
	else if (pHandle->op == 1)//write
	{
		pHandle->pThreadPoll = threadpoll_create(2, MAX_THREAD_NUM, MAX_TASK_SIZE);
		pHandle->fp = file_open(context->path, mode);
		if (NULL == pHandle->fp)
		{
			return NULL;
		}
	}
	return (void *)pHandle;
}

STATIC void auto_unzip(MFILE *stream)
{
	if (NULL == stream)
	{
		return;
	}
	struct mfile_session *pHandle = (struct mfile_session *)stream;
	fflush(pHandle->fp);
	mfile_unzip(pHandle->path, pHandle->download_path);
}

int mfclose(MFILE *stream)
{
	if (NULL == stream)
	{
		return -1;
	}
	auto_unzip(stream);
	struct mfile_session *pHandle = (struct mfile_session *)stream;
	pthread_mutex_lock(&pHandle->session_lock);
	pHandle->is_open = 0;
	pthread_mutex_unlock(&pHandle->session_lock);
	if (pHandle->pThreadPoll != NULL)
	{
		threadpoll_destroy(pHandle->pThreadPoll);
		pHandle->pThreadPoll = NULL;
	}
	pthread_mutex_destroy(&pHandle->session_lock);
	if (pHandle->uzip_param_array != NULL)
	{
		free(pHandle->uzip_param_array);
		pHandle->uzip_param_array = NULL;
	}
	if (pHandle->fp != NULL)
	{
		fclose(pHandle->fp);
		pHandle->fp = NULL;
	}
	if (pHandle->head_buf != NULL)
	{
		free(pHandle->head_buf);
		pHandle->head_buf = NULL;
	}
	if (pHandle->sfile_array != NULL)
	{
		free(pHandle->sfile_array);
		pHandle->sfile_array = NULL;
	}
	free(pHandle);
	pHandle = NULL;
	return 0;
}

int mfseek(MFILE *stream, long offset, int whence)
{
	if (NULL == stream)
	{
		return -1;
	}
	int ret = -1;
	int i = 0;
	struct mfile_session *pHandle = (struct mfile_session *)stream;
	if (pHandle->op == 0)//read
	{
		if (offset < pHandle->head_size)
		{
			if (pHandle->cur_offset >= pHandle->head_size)
			{
				if (pHandle->fp != NULL)
				{
					fclose(pHandle->fp);
					pHandle->fp = NULL;
				}
			}
			pHandle->cur_offset = offset;
			return 0;
		}
		uint64_t cur_offset = pHandle->head_size;
		uint64_t slide_window = pHandle->head_size;
		for (i = 0; i < pHandle->sfile_num; i++)
		{
			slide_window += pHandle->sfile_array[i].file_size;
			if (offset < slide_window)
			{
				uint64_t tmp_offset = pHandle->sfile_array[i].file_size - (slide_window - offset);
				if (pHandle->cur_offset < slide_window - pHandle->sfile_array[i].file_size || pHandle->cur_offset >= slide_window)
				{
					if (pHandle->fp != NULL)
					{
						fclose(pHandle->fp);
						pHandle->fp = NULL;
					}
					pHandle->fp = file_open(pHandle->sfile_array[i].file_abs, "rb");
					if (NULL == pHandle->fp)
					{
						return -1;
					}
				}
				uint64_t off = 0;
				#ifdef WIN32
				ret = fseeko64(pHandle->fp, tmp_offset, SEEK_SET);
				off = ftello64(pHandle->fp);
				#else
				ret = fseeko(pHandle->fp, tmp_offset, SEEK_SET);
				off = ftello(pHandle->fp);
				#endif
				if (ret < 0)
				{
					return -1;
				}
				cur_offset += off;
				break;
			}
			cur_offset += pHandle->sfile_array[i].file_size;
		}
		pHandle->cur_offset = cur_offset;
		ret = 0;
	}	
	else if (pHandle->op == 1)//write
	{
		uint64_t off = 0;
		#ifdef WIN32
		ret = fseeko64(pHandle->fp, offset, SEEK_SET);
		off = ftello64(pHandle->fp);
		#else
		ret = fseeko(pHandle->fp, offset, SEEK_SET);
		off = ftello(pHandle->fp);
		#endif
		if (ret < 0)
		{
			return -1;
		}
		pHandle->cur_offset = off;
		pHandle->latest_write_len = off;
		//unzip_latest_sfile(pHandle);
	}
	return ret;
}

int mfread(void *ptr, size_t size, size_t nmemb, MFILE *stream)
{
	if (NULL == stream)
	{
		return -1;
	}
	int ret = -1;
	int i = 0;
	int read_size = 0;
	struct mfile_session *pHandle = (struct mfile_session *)stream;
	if (pHandle->op == 0)//read
	{
		int cnt = nmemb * size;
		if (pHandle->cur_offset < pHandle->head_size)
		{
			ret = cnt;
			if (pHandle->cur_offset + cnt > pHandle->head_size)
			{
				ret = (int)(pHandle->head_size - pHandle->cur_offset);
			}
			memcpy(ptr, (void *)(pHandle->head_buf + pHandle->cur_offset), ret);
			pHandle->cur_offset += ret;
			read_size += ret;
			if (cnt - ret == 0)
			{
				if (pHandle->cur_offset == pHandle->head_size && pHandle->sfile_num > 0)
				{
					if (pHandle->fp != NULL)
					{
						fclose(pHandle->fp);
						pHandle->fp = NULL;
					}
					pHandle->fp = file_open(pHandle->sfile_array[0].file_abs, "rb");
					if (NULL == pHandle->fp)
					{
						return -1;
					}
				}
				return read_size;
			}
			cnt = cnt - ret;
		}
		uint64_t slide_window = pHandle->head_size;
		for (i = 0; i < pHandle->sfile_num; i++)
		{
			if (pHandle->cur_offset == slide_window)
			{
				if (pHandle->fp != NULL)
				{
					fclose(pHandle->fp);
					pHandle->fp = NULL;
				}
				pHandle->fp = file_open(pHandle->sfile_array[i].file_abs, "rb");
				if (NULL == pHandle->fp)
				{
					return -1;
				}
			}
			slide_window += pHandle->sfile_array[i].file_size;
			if (pHandle->cur_offset < slide_window)
			{
				ret = fread((void *)((char *)ptr + read_size), 1, cnt, pHandle->fp);
				if (ret < 0)
				{
					return -1;
				}
				pHandle->cur_offset += ret;
				read_size += ret;
				if (cnt - ret == 0)
				{
					if (pHandle->cur_offset == slide_window && (i + 1) < pHandle->sfile_num)
					{
						if (pHandle->fp != NULL)
						{
							fclose(pHandle->fp);
							pHandle->fp = NULL;
						}
						pHandle->fp = file_open(pHandle->sfile_array[i+1].file_abs, "rb");
						if (NULL == pHandle->fp)
						{
							return -1;
						}
					}
					return read_size;
				}
				cnt = cnt - ret;
			}
		}
	}
	return read_size;
}

size_t mfwrite(const void *ptr, size_t size, size_t nmeb, MFILE *stream)
{
	if (NULL == stream)
	{
		return -1;
	}
	int ret = -1;
	struct mfile_session *pHandle = (struct mfile_session *)stream;
	if (pHandle->op == 1)//write
	{
		ret = fwrite(ptr, 1, nmeb * size, pHandle->fp);
		if (ret < 0)
		{
			return -1;
		}
		pHandle->cur_offset += ret;
		pHandle->latest_write_len += ret;
		//unzip_latest_sfile(pHandle);
	}
	return ret;
}
