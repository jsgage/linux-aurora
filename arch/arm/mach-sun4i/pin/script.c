/*
**********************************************************************************************************************
*											        eGon
*						                     the Embedded System
*									       script parser sub-system
*
*						  Copyright(C), 2006-2010, SoftWinners Microelectronic Co., Ltd.
*                                           All Rights Reserved
*
* File    : script.c
*
* By      : Jerry
*
* Version : V2.00
*
* Date	  :
*
* Descript:
**********************************************************************************************************************
*/
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>

#include <mach/script_v2.h>
#include <mach/script_i.h>

static  char  *script_mod_buf = NULL;           //指向第一个主键
static  int    script_main_key_count = 0;       //保存主键的个数

/*
************************************************************************************************************
*
*                                             _test_str_length
*
*    函数名称：测试传进的字符串的长度
*
*    参数列表：str
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
static  int   _test_str_length(char *str)
{
	int length = 0;

	while(str[length++])
	{
		if(length > 32)
		{
			length = 32;
			break;
		}
	}

	return length;
}
/*
************************************************************************************************************
*
*                                             script_parser_init
*
*    函数名称：
*
*    参数列表：script_buf: 脚本数据池
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
int script_parser_init(char *script_buf)
{
	script_head_t   *script_head;

	pr_debug("%s(%d)-%s, script_buf addr is %p:\n",__FILE__,__LINE__,__FUNCTION__, script_buf);
	if(script_buf)
	{
		script_mod_buf = script_buf;
		script_head = (script_head_t *)script_mod_buf;

		script_main_key_count = script_head->main_key_count;

		pr_debug("succeed: %s(%d)-%s\n",__FILE__,__LINE__,__FUNCTION__);
		return SCRIPT_PARSER_OK;
	}
	else
	{
		pr_warning("failed: %s(%d)-%s\n",__FILE__,__LINE__,__FUNCTION__);
		return SCRIPT_PARSER_EMPTY_BUFFER;
	}
}
/*
************************************************************************************************************
*
*                                             script_parser_exit
*
*    函数名称：
*
*    参数列表：NULL
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
int script_parser_exit(void)
{
	script_mod_buf = NULL;
	script_main_key_count = 0;

	return SCRIPT_PARSER_OK;
}

/*
************************************************************************************************************
*
*                                             script_parser_fetch
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：根据传进的主键，子键，取得对应的数值
*
*
************************************************************************************************************
*/
int script_parser_fetch(char *main_name, char *sub_name, int value[], int count)
{
	char   main_bkname[32], sub_bkname[32];
	char   *main_char, *sub_char;
	script_main_key_t  *main_key = NULL;
	script_sub_key_t   *sub_key = NULL;
	int    i, j;
	int    pattern, word_count;

	pr_debug("enter script parse fetch. \n");
	//检查脚本buffer是否存在
	if(!script_mod_buf)
	{
		return SCRIPT_PARSER_EMPTY_BUFFER;
	}
	//检查主键名称和子键名称是否为空
	if((main_name == NULL) || (sub_name == NULL))
	{
		return SCRIPT_PARSER_KEYNAME_NULL;
	}
	//检查数据buffer是否为空
	if(value == NULL)
	{
		return SCRIPT_PARSER_DATA_VALUE_NULL;
	}
	//保存主键名称和子键名称，如果超过31字节则截取31字节
	main_char = main_name;
	if(_test_str_length(main_name) > 31)
	{
		memset(main_bkname, 0, 32);
		strncpy(main_bkname, main_name, 31);
		main_char = main_bkname;
	}
	sub_char = sub_name;
	if(_test_str_length(sub_name) > 31)
	{
		memset(sub_bkname, 0, 32);
		strncpy(sub_bkname, sub_name, 31);
		sub_char = sub_bkname;
	}
	pr_debug("gpio: main name is : %s, sub_name is: %s", main_char, sub_char);

	for(i=0;i<script_main_key_count;i++)
	{
		main_key = (script_main_key_t *)(script_mod_buf + (sizeof(script_head_t)) + i * sizeof(script_main_key_t));
		if(strcmp(main_key->main_name, main_char))    //如果主键不匹配，寻找下一个主键
		{
			continue;
		}
		//主键匹配，寻找子键名称匹配
		for(j=0;j<main_key->lenth;j++)
		{
			sub_key = (script_sub_key_t *)(script_mod_buf + (main_key->offset<<2) + (j * sizeof(script_sub_key_t)));
			if(strcmp(sub_key->sub_name, sub_char))    //如果主键不匹配，寻找下一个主键
			{
				continue;
			}
			pattern    = (sub_key->pattern>>16) & 0xffff;             //获取数据的类型
			word_count = (sub_key->pattern>> 0) & 0xffff;             //获取所占用的word个数
			pr_debug("pattern is: 0x%x, word_count is: 0x%x, ", pattern, word_count);
			//取出数据
			switch(pattern)
			{
				case SCIRPT_PARSER_VALUE_TYPE_SINGLE_WORD:            //单word数据类型
					value[0] = *(int *)(script_mod_buf + (sub_key->offset<<2));
					break;

				case SCIRPT_PARSER_VALUE_TYPE_STRING:     			  //字符串数据类型
					if(count < word_count)
					{
						word_count = count;
					}
					memcpy((char *)value, script_mod_buf + (sub_key->offset<<2), word_count << 2);
					break;

				case SCIRPT_PARSER_VALUE_TYPE_MULTI_WORD:
					break;
				case SCIRPT_PARSER_VALUE_TYPE_GPIO_WORD:			   //多word数据类型
				{
					script_gpio_set_t  *user_gpio_cfg = (script_gpio_set_t *)value;
                    //发现是GPIO类型，检查是否足够存放用户数据
                    if(sizeof(script_gpio_set_t) > (count<<2))
                    {
                        return SCRIPT_PARSER_BUFFER_NOT_ENOUGH;
                    }
					strcpy( user_gpio_cfg->gpio_name, sub_char);
					memcpy(&user_gpio_cfg->port, script_mod_buf + (sub_key->offset<<2),  sizeof(script_gpio_set_t) - 32);
					break;
			    }
			    case SCIRPT_PARSER_VALUE_TYPE_NULL:
			    {
					return SCRIPT_PARSER_VALUE_EMPTY;
			    }
			}

			return SCRIPT_PARSER_OK;
		}
	}

	return SCRIPT_PARSER_KEY_NOT_FIND;
}
EXPORT_SYMBOL(script_parser_fetch);
/*
************************************************************************************************************
*
*                                             script_parser_fetch_ex
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：根据传进的主键，子键，取得对应的数值以及数值类型
*
*
************************************************************************************************************
*/
int script_parser_fetch_ex(char *main_name, char *sub_name, int value[], script_parser_value_type_t *type, int count)
{
	char   main_bkname[32], sub_bkname[32];
	char   *main_char, *sub_char;
	script_main_key_t  *main_key = NULL;
	script_sub_key_t   *sub_key = NULL;
	int    i, j;
	int    pattern, word_count;
	script_parser_value_type_t *value_type = type;

	pr_debug("enter script parse fetch. \n");
	//检查脚本buffer是否存在
	if(!script_mod_buf)
	{
		return SCRIPT_PARSER_EMPTY_BUFFER;
	}
	//检查主键名称和子键名称是否为空
	if((main_name == NULL) || (sub_name == NULL))
	{
		return SCRIPT_PARSER_KEYNAME_NULL;
	}
	//检查数据buffer是否为空
	if(value == NULL)
	{
		return SCRIPT_PARSER_DATA_VALUE_NULL;
	}
	//保存主键名称和子键名称，如果超过31字节则截取31字节
	main_char = main_name;
	if(_test_str_length(main_name) > 31)
	{
		memset(main_bkname, 0, 32);
		strncpy(main_bkname, main_name, 31);
		main_char = main_bkname;
	}
	sub_char = sub_name;
	if(_test_str_length(sub_name) > 31)
	{
		memset(sub_bkname, 0, 32);
		strncpy(sub_bkname, sub_name, 31);
		sub_char = sub_bkname;
	}
	pr_debug("gpio: main name is : %s, sub_name is: %s", main_char, sub_char);

	for(i=0;i<script_main_key_count;i++)
	{
		main_key = (script_main_key_t *)(script_mod_buf + (sizeof(script_head_t)) + i * sizeof(script_main_key_t));
		if(strcmp(main_key->main_name, main_char))    //如果主键不匹配，寻找下一个主键
		{
			continue;
		}
		//主键匹配，寻找子键名称匹配
		for(j=0;j<main_key->lenth;j++)
		{
			sub_key = (script_sub_key_t *)(script_mod_buf + (main_key->offset<<2) + (j * sizeof(script_sub_key_t)));
			if(strcmp(sub_key->sub_name, sub_char))    //如果主键不匹配，寻找下一个主键
			{
				continue;
			}
			pattern    = (sub_key->pattern>>16) & 0xffff;             //获取数据的类型
			word_count = (sub_key->pattern>> 0) & 0xffff;             //获取所占用的word个数
			pr_debug("pattern is: 0x%x, word_count is: 0x%x, ", pattern, word_count);
			//取出数据
			switch(pattern)
			{
				case SCIRPT_PARSER_VALUE_TYPE_SINGLE_WORD:            //单word数据类型
					value[0] = *(int *)(script_mod_buf + (sub_key->offset<<2));
					*value_type = SCIRPT_PARSER_VALUE_TYPE_SINGLE_WORD;
					break;

				case SCIRPT_PARSER_VALUE_TYPE_STRING:     			  //字符串数据类型
					if(count < word_count)
					{
						word_count = count;
					}
					memcpy((char *)value, script_mod_buf + (sub_key->offset<<2), word_count << 2);
					*value_type = SCIRPT_PARSER_VALUE_TYPE_STRING;
					break;

				case SCIRPT_PARSER_VALUE_TYPE_MULTI_WORD:
					*value_type = SCIRPT_PARSER_VALUE_TYPE_MULTI_WORD;
					break;
				case SCIRPT_PARSER_VALUE_TYPE_GPIO_WORD:			   //多word数据类型
				{
					script_gpio_set_t  *user_gpio_cfg = (script_gpio_set_t *)value;
                    //发现是GPIO类型，检查是否足够存放用户数据
                    if(sizeof(script_gpio_set_t) > (count<<2))
                    {
                        return SCRIPT_PARSER_BUFFER_NOT_ENOUGH;
                    }
					strcpy( user_gpio_cfg->gpio_name, sub_char);
					memcpy(&user_gpio_cfg->port, script_mod_buf + (sub_key->offset<<2),  sizeof(script_gpio_set_t) - 32);
					*value_type = SCIRPT_PARSER_VALUE_TYPE_GPIO_WORD;
					break;
			    }
			}

			return SCRIPT_PARSER_OK;
		}
	}

	return SCRIPT_PARSER_KEY_NOT_FIND;
}
EXPORT_SYMBOL(script_parser_fetch_ex);
/*
************************************************************************************************************
*
*                                             eGon2_script_parser_patch
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：根据传进的主键，子键，键值，填充对应的数值
*
*
************************************************************************************************************
*/
int script_parser_patch(char *main_name, char *sub_name, int value)
{
	char   main_bkname[32], sub_bkname[32];
	char   *main_char, *sub_char;
	script_main_key_t  *main_key = NULL;
	script_sub_key_t   *sub_key = NULL;
	int    i, j;
	int    pattern;

	//检查脚本buffer是否存在
	if(!script_mod_buf)
	{
		return SCRIPT_PARSER_EMPTY_BUFFER;
	}
	//检查主键名称和子键名称是否为空
	if((main_name == NULL) || (sub_name == NULL))
	{
		return SCRIPT_PARSER_KEYNAME_NULL;
	}
	//检查数据buffer是否为空
	if(value == NULL)
	{
		return SCRIPT_PARSER_DATA_VALUE_NULL;
	}
	//保存主键名称和子键名称，如果超过31字节则截取31字节
	main_char = main_name;
	if(_test_str_length(main_name) > 31)
	{
	    memset(main_bkname, 0, 32);
		strncpy(main_bkname, main_name, 31);
		main_char = main_bkname;
	}
    sub_char = sub_name;
	if(_test_str_length(sub_name) > 31)
	{
		memset(sub_bkname, 0, 32);
		strncpy(sub_bkname, sub_name, 31);
		sub_char = sub_bkname;
	}
	for(i=0;i<script_main_key_count;i++)
	{
		main_key = (script_main_key_t *)(script_mod_buf + (sizeof(script_head_t)) + i * sizeof(script_main_key_t));
		if(strcmp(main_key->main_name, main_char))    //如果主键不匹配，寻找下一个主键
		{
			continue;
		}
		//主键匹配，寻找子键名称匹配
		for(j=0;j<main_key->lenth;j++)
		{
			sub_key = (script_sub_key_t *)(script_mod_buf + (main_key->offset<<2) + (j * sizeof(script_sub_key_t)));
			if(strcmp(sub_key->sub_name, sub_char))    //如果主键不匹配，寻找下一个主键
			{
				continue;
			}
			pattern    = (sub_key->pattern>>16) & 0xffff;             //获取数据的类型
			//取出数据
			if(pattern == SCIRPT_PARSER_VALUE_TYPE_SINGLE_WORD)       //单word数据类型
			{
				*(int *)(script_mod_buf + (sub_key->offset<<2)) = value;

				return SCRIPT_PARSER_OK;
			}
		}
	}

	return SCRIPT_PARSER_KEY_NOT_FIND;
}
EXPORT_SYMBOL(script_parser_patch);
/*
************************************************************************************************************
*
*                                             script_parser_subkey_count
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：根据传进的主键，取得主键下的子键个数
*
*
************************************************************************************************************
*/
int script_parser_subkey_count(char *main_name)
{
	char   main_bkname[32];
	char   *main_char;
	script_main_key_t  *main_key = NULL;
	int    i;

	//检查脚本buffer是否存在
	if(!script_mod_buf)
	{
		return SCRIPT_PARSER_EMPTY_BUFFER;
	}
	//检查主键名称和子键名称是否为空
	if(main_name == NULL)
	{
		return SCRIPT_PARSER_KEYNAME_NULL;
	}
	//保存主键名称和子键名称，如果超过31字节则截取31字节
	main_char = main_name;
	if(_test_str_length(main_name) > 31)
	{
	    memset(main_bkname, 0, 32);
		strncpy(main_bkname, main_name, 31);
		main_char = main_bkname;
	}

	for(i=0;i<script_main_key_count;i++)
	{
		main_key = (script_main_key_t *)(script_mod_buf + (sizeof(script_head_t)) + i * sizeof(script_main_key_t));
		if(strcmp(main_key->main_name, main_char))    //如果主键不匹配，寻找下一个主键
		{
			continue;
		}

		return main_key->lenth;    //返回当前主键下的子键个数
	}

	return -1;                     //-1 表示没有对应的主键
}
/*
************************************************************************************************************
*
*                                             script_parser_mainkey_count
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：根据传进的主键，取得主键的个数
*
*
************************************************************************************************************
*/
int script_parser_mainkey_count(void)
{
	//检查脚本buffer是否存在
	if(!script_mod_buf)
	{
		return SCRIPT_PARSER_EMPTY_BUFFER;
	}

	return 	script_main_key_count;
}
/*
************************************************************************************************************
*
*                                             script_parser_mainkey_get_gpio_count
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：根据传进的主键，取得主键的GPIO个数
*
*
************************************************************************************************************
*/
int script_parser_mainkey_get_gpio_count(char *main_name)
{
	char   main_bkname[32];
	char   *main_char;
	script_main_key_t  *main_key = NULL;
	script_sub_key_t   *sub_key = NULL;
	int    i, j;
	int    pattern, gpio_count = 0;

	//检查脚本buffer是否存在
	if(!script_mod_buf)
	{
		return SCRIPT_PARSER_EMPTY_BUFFER;
	}
	//检查主键名称和子键名称是否为空
	if(main_name == NULL)
	{
		return SCRIPT_PARSER_KEYNAME_NULL;
	}
	//保存主键名称和子键名称，如果超过31字节则截取31字节
	main_char = main_name;
	if(_test_str_length(main_name) > 31)
	{
	    memset(main_bkname, 0, 32);
		strncpy(main_bkname, main_name, 31);
		main_char = main_bkname;
	}

	for(i=0;i<script_main_key_count;i++)
	{
		main_key = (script_main_key_t *)(script_mod_buf + (sizeof(script_head_t)) + i * sizeof(script_main_key_t));
		if(strcmp(main_key->main_name, main_char))    //如果主键不匹配，寻找下一个主键
		{
			continue;
		}
		//主键匹配，寻找子键名称匹配
		for(j=0;j<main_key->lenth;j++)
		{
			sub_key = (script_sub_key_t *)(script_mod_buf + (main_key->offset<<2) + (j * sizeof(script_sub_key_t)));

			pattern    = (sub_key->pattern>>16) & 0xffff;             //获取数据的类型
			//取出数据
			if(SCIRPT_PARSER_VALUE_TYPE_GPIO_WORD == pattern)
			{
				gpio_count ++;
			}
		}
	}

	return gpio_count;
}

/*
************************************************************************************************************
*
*                                             script_parser_mainkey_get_gpio_cfg
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：根据传进的主键，取得主键下GPIO的配置信息
*
*
************************************************************************************************************
*/
int script_parser_mainkey_get_gpio_cfg(char *main_name, void *gpio_cfg, int gpio_count)
{
	char   main_bkname[32];
	char   *main_char;
	script_main_key_t  *main_key = NULL;
	script_sub_key_t   *sub_key = NULL;
	script_gpio_set_t  *user_gpio_cfg = (script_gpio_set_t *)gpio_cfg;
	int    i, j;
	int    pattern, user_index;

	//检查脚本buffer是否存在
	if(!script_mod_buf)
	{
		return SCRIPT_PARSER_EMPTY_BUFFER;
	}
	//检查主键名称和子键名称是否为空
	if(main_name == NULL)
	{
		return SCRIPT_PARSER_KEYNAME_NULL;
	}
	//首先清空用户buffer
	memset(user_gpio_cfg, 0, sizeof(script_gpio_set_t) * gpio_count);
	//保存主键名称和子键名称，如果超过31字节则截取31字节
	main_char = main_name;
	if(_test_str_length(main_name) > 31)
	{
	    memset(main_bkname, 0, 32);
		strncpy(main_bkname, main_name, 31);
		main_char = main_bkname;
	}

	for(i=0;i<script_main_key_count;i++)
	{
		main_key = (script_main_key_t *)(script_mod_buf + (sizeof(script_head_t)) + i * sizeof(script_main_key_t));
		if(strcmp(main_key->main_name, main_char))    //如果主键不匹配，寻找下一个主键
		{
			continue;
		}
		//主键匹配，寻找子键名称匹配
		pr_debug("mainkey name = %s\n", main_key->main_name);
		user_index = 0;
		for(j=0;j<main_key->lenth;j++)
		{
			sub_key = (script_sub_key_t *)(script_mod_buf + (main_key->offset<<2) + (j * sizeof(script_sub_key_t)));
			pr_debug("subkey name = %s\n", sub_key->sub_name);
			pattern    = (sub_key->pattern>>16) & 0xffff;             //获取数据的类型
			pr_debug("subkey pattern = %d\n", pattern);
			//取出数据
			if(SCIRPT_PARSER_VALUE_TYPE_GPIO_WORD == pattern)
			{
			    strcpy( user_gpio_cfg[user_index].gpio_name, sub_key->sub_name);
				memcpy(&user_gpio_cfg[user_index].port, script_mod_buf + (sub_key->offset<<2), sizeof(script_gpio_set_t) - 32);
				user_index++;
				if(user_index >= gpio_count)
				{
					break;
				}
			}
		}
		return SCRIPT_PARSER_OK;
	}

	return SCRIPT_PARSER_KEY_NOT_FIND;
}

