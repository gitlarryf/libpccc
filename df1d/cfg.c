/*
 * This file is part of df1d.
 * Allen Bradley DF1 link layer service.
 * Copyright (C) 2007 Jason Valenzuela
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Design Systems Partners
 * Attn: Jason Valenzuela
 * 2516 JMT Industrial Drive, Suite 112
 * Apopka, FL  32703
 * jvalenzuela <at> dspfl <dot> com
 */

#include "df1.h"

xmlDocPtr doc;
iconv_t utf8_conv; /* To convert UTF-8 returned from libxml to char. */

static int xml_validate(void);
static int xml_parse_root(void);
static void xml_parse_conn(xmlNode *conn_node);
static int get_param_val(xmlNodePtr src, char *dst);
static int get_name(const char *val, char *dst);
static int get_duplex(const char *name, const char *val, DUPLEX_T *dst);
static int get_error_detect(const char *name, const char *val, int *dst);
static int get_tty_dev(const char *name, const char *val, char *dst);
static int get_tty_rate(const char *name, const char *val, int *dst);
static int get_sock_port(const char *name, const char *val, in_port_t *dst);
static int get_dup_detect(const char *name, const char *val, int *dst);
static int get_max_nak(const char *name, const char *val, unsigned int *dst);
static int get_max_enq(const char *name, const char *val, unsigned int *dst);
static int get_ack_timeout(const char *name, const char *val,
			   unsigned int *dst);

/*
 * Description : Reads the XML configuration file and initializes the
 *               connections.
 *
 * Arguments :
 *
 * Return Value :
 */
extern int cfg_read(const char *file)
{
  int ret = 0;
  xmlLineNumbersDefault(1);
  /*
   * libXML returns all text in UTF-8 encoding. Setup iconv to convert
   * from UTF-8 to char.
   */
  utf8_conv = iconv_open("", "UTF-8");
  if (utf8_conv == (iconv_t)-1)
    {
      log_msg(LOG_ERR, "%s:%d Error opening UTF-8 converter : %s\n", __FILE__,
	      __LINE__, strerror(errno));
      return -1;
    }
  doc = xmlReadFile(file, NULL, 0);
  if (doc == NULL)
    {
      log_msg(LOG_ERR, "%s:%d Unable to read configuration file.",
	      __FILE__, __LINE__);
      ret = -1;
    }
  else
    {
      if (xml_validate())
	{
	  xml_parse_root();
	  
	}
      xmlFreeDoc(doc);
    }
  xmlCleanupParser();
  if (iconv_close(utf8_conv))
    log_msg(LOG_ERR, "%s:%d Error closing UTF-8 converter : %s\n", __FILE__,
	    __LINE__, strerror(errno));
  return ret;
}

/*
 * Description : Validates the configuration file.
 *
 * Arguments : None.
 *
 * Return Value : Non-zero if the configuration file is valid.
 *                Zero if the XML is invalid.
 */
static int xml_validate(void)
{
  return 1;
  static const char dtd_str[] = \
    "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"
    "<!DOCTYPE df1d_config [\n"
    "<!ELEMENT df1d_config (connection+)>\n"
    "<!ELEMENT connection (name,duplex,error_detect,device,baud,port,duplicate_detect,max_nak,max_enq)>\n"
    "<!ELEMENT name (#PCDATA)>\n"
    "<!ELEMENT duplex (#PCDATA>\n"
    "<!ELEMENT error_detect (#PCDATA)>\n"
    "<!ELEMENT device (#PCDATA)>\n"
    "<!ELEMENT baud (#PCDATA)>\n"
    "<!ELEMENT port (#PCDATA)>\n"
    "<!ELEMENT duplicate_detect (#PCDATA)>\n"
    "<!ELEMENT max_nak (#PCDATA)>\n"
    "<!ELEMENT max_enq (#PCDATA)>\n"
    "]>\n"
    "<start/>\n";
  xmlValidCtxtPtr ctxt;
  xmlDocPtr dtd_doc;
  xmlDtdPtr dtd;
  int ret;
  dtd_doc = xmlReadMemory(dtd_str, strlen(dtd_str), NULL, NULL, 0);
  if (dtd_doc == NULL)
    {
      log_msg(LOG_ERR, "%s:%d Compiled DTD invalid.", __FILE__, __LINE__);
      return 0;
    }
  else
    {
      dtd = xmlNewDtd(dtd_doc, "df1d_config", NULL, NULL);
      ctxt = xmlNewValidCtxt();
      if (ctxt == NULL)
	{
	  
	  
	  return 0;
	}
      else
	{
	  if (xmlValidateDtd(ctxt, doc, dtd))
	    {
	      log_msg(LOG_DEBUG,
		      "%s:%d Configuration file XML validation passed.",
		      __FILE__, __LINE__);
	      ret = 1;
	    }
	  else
	    {
	      log_msg(LOG_ERR,
		      "%s:%d Configuration file failed XML validation.",
		      __FILE__, __LINE__);
	      ret = 0;
	    }
	  xmlFreeValidCtxt(ctxt);
	  xmlFreeDtd(dtd);
	}
      xmlFreeDoc(dtd_doc);
    }
  return ret;
}

/*
 * Description : Parses the root element.
 *
 * Arguments : None.
 *
 * Return Value : None.
 */
static int xml_parse_root(void)
{
  xmlNode *node = xmlDocGetRootElement(doc);
  for (node = node->xmlChildrenNode; node != NULL; node = node->next)
    if ((node->type == XML_ELEMENT_NODE) &&
	xmlStrEqual(node->name, (const xmlChar *)"connection"))
      xml_parse_conn(node);
  return 0;
}

/*
 * Description : Parses a connection element.
 *
 * Arguments : conn_node - Pointer to the connection element.
 *
 * Return Value : None.
 */
static void xml_parse_conn(xmlNode *conn_node)
{
  char name[CONN_NAME_LEN];
  char tty_dev[PATH_MAX];
  DUPLEX_T duplex;
  int tty_rate;
  int use_crc;
  in_port_t sock_port;
  unsigned int tx_max_nak;
  unsigned int tx_max_enq;
  int rx_dup_detect;
  unsigned int ack_timeout;
  xmlNode *param;
  for (param = conn_node->xmlChildrenNode; param != NULL; param = param->next)
    if (param->type == XML_ELEMENT_NODE)
      {
	char val[PATH_MAX];
	if (get_param_val(param, val)) return;
	if (xmlStrEqual(param->name, (const xmlChar *)"name"))
	  {
	    if (get_name(val, name)) return;
	    continue;
	  }
	if (xmlStrEqual(param->name, (const xmlChar *)"duplex"))
	  {
	    if (get_duplex(name, val, &duplex)) return;
	    continue;
	  }
	if (xmlStrEqual(param->name, (const xmlChar *)"error_detect"))
	  {
	    if (get_error_detect(name, val, &use_crc)) return;
	    continue;
	  }
	if (xmlStrEqual(param->name, (const xmlChar *)"device"))
	  {
	    if (get_tty_dev(name, val, tty_dev)) return;
	    continue;
	  }
	if (xmlStrEqual(param->name, (const xmlChar *)"baud"))
	  {
	    if (get_tty_rate(name, val, &tty_rate)) return;
	    continue;
	  }
	if (xmlStrEqual(param->name, (const xmlChar *)"port"))
	  {
	    if (get_sock_port(name, val, &sock_port)) return;
	    continue;
	  }
	if (xmlStrEqual(param->name, (const xmlChar *)"duplicate_detect"))
	  {
	    if (get_dup_detect(name, val, &rx_dup_detect)) return;
	    continue;
	  }
	if (xmlStrEqual(param->name, (const xmlChar *)"max_nak"))
	  {
	    if (get_max_nak(name, val, &tx_max_nak)) return;
	    continue;
	  }
	if (xmlStrEqual(param->name, (const xmlChar *)"max_enq"))
	  {
	    if (get_max_enq(name, val, &tx_max_enq)) return;
	    continue;
	  }
	if (xmlStrEqual(param->name, (const xmlChar *)"ack_timeout"))
	  {
	    if (get_ack_timeout(name, val, &ack_timeout)) return;
	    continue;
	  }
      }
  conn_init(name, tty_dev, tty_rate, use_crc, sock_port, tx_max_nak,
	    tx_max_enq, rx_dup_detect, ack_timeout);
  return; 
}

/*
 * Description : Retrieves the text content of a node and converts it from
 *               UTF-8 to char. The converted string will be NULL terminated.
 *
 * Arguments : src - The source node from which to extract the content.
 *             dst - Location to store the retrieved text content.
 *
 * Return Value : Zero if the parameter was retrieved and converted
 *                successfully.
 *                Non-zero if an error occured.
 */
static int get_param_val(xmlNodePtr src, char *dst)
{
  size_t i, val_max;
  size_t dst_max = PATH_MAX - 1;
  xmlChar *val;
  const char *p;
  val = xmlNodeListGetString(doc, src->xmlChildrenNode, 1);
  /*
   * The original string starting location must be saved as iconv()
   * will modify its value.
   */
  p = (char *)val;
  val_max = xmlStrlen(val);
  i = iconv(utf8_conv, (char **)&p, &val_max, &dst, &dst_max);
  xmlFree(val);
  if (i == (size_t)-1)
    {
      log_msg(LOG_ERR, "%s:%d Error converting configuration"
	      " parameter at line %ld : %s", __FILE__, __LINE__,
	      xmlGetLineNo(src), strerror(errno));
      return -1;
    }
  *dst = 0; /* Null terminate the converted string. */
  return 0;
}

/*
 * Description : Gets the connection's name from the 'name' element.
 *
 * Arguments : val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_name(const char *val, char *dst)
{
  strncpy(dst, val, CONN_NAME_LEN);
  if (!strlen(dst))
    {
      log_msg(LOG_ERR, "%s:%d No connection name specified.", __FILE__,
		  __LINE__);
      return -1;
    }
  return 0;
}

/*
 * Description : Gets the connection's duplex mode from the 'duplex' element.
 *
 * Arguments : name - Connection name.
 *             val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_duplex(const char *name, const char *val, DUPLEX_T *dst)
{
  if (!strcasecmp(val, "full")) *dst = DUPLEX_FULL;
  else if (!strcasecmp(val, "master")) *dst = DUPLEX_MASTER;
  else if (!strcasecmp(val, "slave")) *dst = DUPLEX_SLAVE;
  else
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error reading duplex mode. "
	      "Valid options are 'full', 'master', and 'slave'.\n", __FILE__,
	      __LINE__, name);
      return 1;
    }
  return 0;
}

/*
 * Description : Get's the connection's error detection method from the
 *               'error_detect' element.
 *
 * Arguments : name - Connection name.
 *             val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_error_detect(const char *name, const char *val, int *dst)
{
  if (!strcasecmp(val, "crc")) *dst = 1;
  else if (!strcasecmp(val, "bcc")) *dst = 0;
  else
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error reading error detection method. "
	      "Valid options are 'crc' and 'bcc'.\n", __FILE__,
	      __LINE__, name);
      return 1;
    }
  return 0;
}

/*
 * Description : Gets the connection's TTY device from the 'device' element.
 *
 * Arguments : name - Connection name.
 *             val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_tty_dev(const char *name, const char *val, char *dst)
{
  strncpy(dst, val, PATH_MAX);
  if (!strlen(dst))
    {
      log_msg(LOG_ERR, "%s:%d [%s] No TTY device specified.\n", __FILE__,
	      __LINE__, name);
      return 1;
    }
  return 0;
}

/*
 * Description : Gets the connection's TTY baud rate from the 'baud' element.
 *
 * Arguments : name - Connection name.
 *             val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_tty_rate(const char *name, const char *val, int *dst)
{
  unsigned int rate;
  const char valid_opts[] =
    "Valid options are 110, 300, 600, 1200, 2400, 9600, 19200, and 38400.";
  if (sscanf(val, "%u", &rate) != 1)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error reading TTY baud rate. %s\n",
	      __FILE__, __LINE__, name, valid_opts);
      return 1;
    }
  switch (rate)
    {
    case 110:
      *dst = B110;
      break;
    case 300:
      *dst = B300;
      break;
    case 600:
      *dst = B600;
      break;
    case 1200:
      *dst = B1200;
      break;
    case 2400:
      *dst = B2400;
      break;
    case 9600:
      *dst = B9600;
      break;
    case 19200:
      *dst = B19200;
      break;
    case 38400:
      *dst = B38400;
      break;
    default:
      log_msg(LOG_ERR, "%s:%d [%s] Illegal TTY baud rate specified. %s\n",
	      __FILE__, __LINE__, name, valid_opts);
      return 1;
    }
  return 0;
}

/*
 * Description : Gets the connection's listening TCP port from the 'port'
 *               element.
 *
 * Arguments : name - Connection name.
 *             val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_sock_port(const char *name, const char *val, in_port_t *dst)
{
  if (sscanf(val, "%u", (unsigned int *)dst) != 1)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error reading socket TCP port number.\n",
	      __FILE__, __LINE__, name);
      return 1;
    }
  return 0;
}

/*
 * Description : Gets the connection's duplicate message detection setting from
 *               the 'duplicate_detect' element.
 *
 * Arguments : name - Connection name.
 *             val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_dup_detect(const char *name, const char *val, int *dst)
{
  if (!strcasecmp(val, "yes")) *dst = 1;
  else if (!strcasecmp(val, "no")) *dst = 0;
  else
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Error reading duplicate message detection option."
	      "Valid options are 'yes' and 'no'.\n",
	      __FILE__, __LINE__, name);
      return 1;
    }
  return 0;
}

/*
 * Description : Gets the connection's maximum allowable NAKs from the
 *               'max_nak' element.
 *
 * Arguments : name - Connection name.
 *             val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_max_nak(const char *name, const char *val, unsigned int *dst)
{
  if (sscanf(val, "%u", dst) != 1)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error reading maximum NAKs.\n", __FILE__,
	      __LINE__, name);
      return 1;
    }
  if (*dst > 255)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Illegal value for maximum NAKs. "
	      " Valid values are 0-255.\n", __FILE__, __LINE__, name);
      return 1;
    }
  return 0;
}

/*
 * Description : Gets the connection's maximum allowable ENQs from the
 *               'max_enq' element.
 *
 * Arguments : name - Connection name.
 *             val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_max_enq(const char *name, const char *val, unsigned int *dst)
{
  if (sscanf(val, "%u", dst) != 1)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error reading maximum ENQs.\n", __FILE__,
	      __LINE__, name);
      return 1;
    }
  if (*dst > 255)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Illegal value for maximum ENQs. Valid values"
	      " are 0-255.\n", __FILE__, __LINE__, name);
      return 1;
    }
  return 0;
}

/*
 * Description : Gets the connection's acknowledge timeout.
 *
 * Arguments : name - Connection name.
 *             val - Pointer to string containing the option.
 *             dst - Pointer to location to store the selection.
 *
 * Return Value : Zero if the given parameter was successfully read.
 *                Non-zero if an error occured or an invalid value was given.
 */
static int get_ack_timeout(const char *name, const char *val,
			   unsigned int *dst)
{
  if (sscanf(val, "%u", dst) != 1)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error reading ACK timeout.\n", __FILE__,
	      __LINE__, name);
      return 1;
    }
  return 0;
}
