/* Radroach is a simple IRC bot
   Copyright (C) 2009  Evgeny Grablyk <evgeny.grablyk@gmail.com>
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include "radroach.h"
#include "basic_actions.c"

void
p_help(void)
{
  printf("Usage: %s [-h] -c confile\n", execname);
}

void
logstr(char *str)
{
  printf("%s: %s", execname, str);
}

void
raw(int sock, char *str)
{
  logstr(str);
  write(sock, str, strlen(str));
}

/* FIXME: store everything in a buffer, give out lines */
char *
sogetline(int s)
{
  char c = 0, *buf, *rebuf;
  int r, i = 0, bsz = BUFSZ;
  
  buf = malloc(BUFSZ);
  while( (r = read(s, &c, 1)) != -1 )
    {
      /* ordinary read */
      if(r)
	buf[i++] = c;
      /* all done, reallocate and return */
      /* string is less than our buffer, reallocate. */
      if(c == '\n' && i < bsz)
	{
	  buf[i] = '\0';
	  rebuf = malloc(strlen(buf) + 1);
	  strcpy(rebuf, buf);
	  free(buf);
	  return rebuf;
	}
      else if(c == '\n' && i == bsz)
	{
	  buf[i - 1] = '\0';
	  return buf;
	}
      /* we've reached buffer limit, reallocate with more space */
      if(i >= bsz)
	{
	  rebuf = malloc(i + BUFSZ);
	  memcpy(rebuf, buf, i);
	  free(buf);
	  buf = rebuf;
	  bsz += BUFSZ;
	}
    }
  /* something failed and we did not return a sting earlier */
  return NULL;
}
      

void
cmdfree(command *cmd)
{
  free(cmd->raw);
  free(cmd);
}

void
msgfree(message *msg)
{
  free(msg->raw);
  free(msg);
}

void
sconnect(char *host)
{
  struct addrinfo *server = NULL, hints;
  int st;
  
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  if( (sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
      error(0, 0, "Socket creation failed.\n");
      goto err;
    }
  logstr("Socket created.\n");
  if( (st = getaddrinfo(host, "ircd", &hints, &server)) != 0 )
    {
      error(0, 0, "Failed to resolve hostname: %s", gai_strerror(st));
      goto err;
    }
  logstr("Hostname resolved.\n");
  if( connect(sock, server->ai_addr, server->ai_addrlen) == -1)
    {
      error(0, 0, "Connection to server failed: %s", gai_strerror(errno));
      goto err;
    }
  logstr("Connected.\n");
  freeaddrinfo(server);
  
  /* inbuf is not used yet, don't scream. */
  sprintf(inbuf, "NICK %s\n", conf->nick);
  raw(sock, inbuf);
  
  return;
  
 err:
  if(server)
  freeaddrinfo(server);
  exit(EXIT_FAILURE);
}
     
void
setup(int sock)
{
  char *l = NULL;
  if(conf->name)
    {
      l = malloc(strlen("USER  localhost  :\n") + strlen(conf->name) + strlen(conf->nick) + strlen(conf->host) + 1);
      sprintf(l, "USER %s localhost %s :%s\n", conf->nick, conf->host, conf->name);
      raw(sock, l);
    }
  else
    {
      l = malloc(strlen("USER  localhost  :CBot - a bot in C, by age\n") + strlen(conf->nick) + strlen(conf->host) + 1);
      sprintf(l, "USER %s localhost %s :CBot - a bot in C, by age\n", conf->nick, conf->host);
      raw(sock, l);
    }
  free(l);
}

/* parse a string, return message containing that string's data */
message *
parsemsg (char *l)
{
  /* we only want to parse private messages, which may contain instructions */
  if(strstr(l, "PRIVMSG") != NULL && l[0] == ':')
    {
      message *result = malloc(sizeof(message));
	
      result->raw = l;
      
      result->sender = &l[1];
      l = strchr(l, '!') + 1;
      *(l - 1) = '\0';

      result->ident = &l[0];
      l = strchr(l, '@') + 1;
      *(l - 1) = '\0';

      result->host = &l[0];
      l = strchr(l, ' ') + 1;
      *(l - 1) = '\0';

      /* we need to skip "PRIVMSG" */
      l = strchr(l, ' ') + 1;
      result->dest = &l[0];
      /* skipping ":" */
      l = strchr(l, ' ') + 2;
      *(l - 2) = '\0';

      /* string ends with \r\n */
      result->msg = &l[0];
      l = strchr(l, '\r');
      *l = '\0';
      
      return result;
    }
  return NULL;
}

command *
parsecmd(char *str)
{
  command *result;
  char msg_regex[] = "^ ([^ ]+) (.*)";
  enum act {RAW, ACT, PARAM };
  regex_t *regex = (regex_t *) malloc (sizeof (regex_t));
  regmatch_t matches[3];

  /* put out trigger char in */
  msg_regex[1] = action_trigger;
  
  regcomp (regex, msg_regex, REG_ICASE | REG_EXTENDED);
  if( regexec (regex, str, 3, matches, 0) == 0)
    {
      result = (command *) malloc (sizeof (command));
      
      result->action = (char *) malloc(matches[ACT].rm_eo - matches[ACT].rm_so + 1);
      memcpy(result->action, &str[matches[ACT].rm_so], matches[ACT].rm_eo - matches[ACT].rm_so);
      result->action[matches[ACT].rm_eo - matches[ACT].rm_so] = '\0';
      
      result->params = (char *) malloc(matches[PARAM].rm_eo - matches[PARAM].rm_so + 1);
      memcpy(result->params, &str[matches[PARAM].rm_so], matches[PARAM].rm_eo - matches[PARAM].rm_so);
      result->params[matches[PARAM].rm_eo - matches[PARAM].rm_so] = '\0';

      result->raw = NULL;
    }
  else
    {
      regfree(regex);
      free(regex);
      return NULL;
    }
  regfree(regex);
  free(regex);
  return result;
}

int
p_response(char *l)
{
  if(strstr(l, "PING ") != NULL)
    {
      memcpy(l, "PONG", 4);
      raw(sock, l);
      free(l);
      if( setup_done == 0)
	{
	  setup(sock);
	  setup_done = 1;
	}
      return 1;
    }
  return 0;
}

settings *
parsecfg(char *cfile)
{
  cfg_t *cfg = NULL;
  settings *conf = (settings *) malloc(sizeof(settings));
  int status;

  cfg_opt_t opts[] = {
    CFG_SIMPLE_STR("nick", &conf->nick),
    CFG_SIMPLE_STR("name", &conf->name),
    CFG_SIMPLE_STR("host", &conf->host),
    CFG_SIMPLE_STR("trusted", &conf->trusted),
    CFG_SIMPLE_STR("password", &conf->password),
    CFG_END()
  };

  /* default values */
  conf->nick = NULL;
  conf->name = NULL;
  conf->host = NULL;
  conf->trusted = NULL;
  conf->password = NULL;

  cfg = cfg_init(opts, 0);
  status = cfg_parse(cfg, cfile);
  printf("%s: Trusted users are: %s\n", execname, conf->trusted);
  /* printf("%s %s %s %s\n", conf->nick, conf->name, conf->host, conf->password); */
  /* check for necessary settings */
  if ( status == CFG_SUCCESS && conf->nick != NULL && conf->host != NULL)
    return conf;
  return NULL;
}

int
checkrights(message *msg)
{
  char *l, *s;

  s = (char *) malloc(1 + strlen(msg->ident) + strlen(msg->host));
  sprintf(s, "%s@%s", msg->ident, msg->host);
  for(l = strtok(conf->trusted, " "); l != NULL; l = strtok(NULL, " "))
    if( strstr(s, l) )
      {
	free(s);
	return 1;
      }
  free(s);
  return 0;
}

void
execute(message *msg, command *cmd)
{
  int i;
  
  for (i = 0; acts[i].name != NULL; ++i)
    if(strstr(acts[i].name, cmd->action))
      {
	if(checkrights(msg))
	  {
	    printf("%s: Accepted command from %s (%s@%s)\n", execname, msg->sender, msg->ident, msg->host);
	    acts[i].exec(msg, cmd);
	    printf("%s: Command '%s' executed with parameters: '%s'\n", execname, acts[i].name, cmd->params);
	    break;
	  }
	else
	  {
	    printf("%s: %s (%s@%s) is not trusted! (tried to execute: %s)\n", execname, msg->sender, msg->ident, msg->host, acts[i].name);
	    break;
	  }
      }
    else if(acts[i + 1].name == NULL)
      {
	printf("%s: No handler found for this command: '%s' (supplied parameters: '%s')\n", execname, cmd->action, cmd->params);
	break;
      }
  cmdfree(cmd);
  msgfree(msg);
}

int
configure(int argc, char *argv[])
{
  char *cfile = NULL;
  int opt;

  if(argc < 2)
    {
      p_help();
      return 0;
    }
  
  execname = strdup(argv[0]);
  
  while ((opt = getopt(argc, argv, "hc:")) != -1)
    {
      switch(opt)
	{
	case 'h':
	  {
	    p_help();
	    continue;
	  }
	case 'c':
	  {
	    cfile = optarg;
	    continue;
	  }
	case '?':
	default:
	  continue;
	}
    }

  if(cfile == NULL)
    {
      error(0, 0, "No configuration file specified.");
      return 0;
    }
  
  printf("%s: Configuration file selected: %s\n", execname, cfile);

  if( (conf = parsecfg(cfile)) == NULL)
    {
      error(0, 0, "You did not specify nickname and host or parsing configuration failed.");
      return 0;
    }
  return 1;
}  

int
main(int argc, char *argv[])
{
  char *l = NULL;
  message *cmsg = NULL;
  command *ccmd = NULL;
  
  if( !configure(argc, argv) )
    exit(EXIT_FAILURE);

  /* printf("%s: This is CBot, commit %s", execname, "VERSION"); */

  sconnect(conf->host);

  while( (l = sogetline(sock)) != NULL )
    {
      if (p_response(l))
	continue;
      cmsg = parsemsg(l);
      if (cmsg != NULL)
	ccmd = parsecmd(cmsg->msg);
      if( ccmd != NULL )
	execute(cmsg, ccmd);
    }
  return EXIT_SUCCESS;
}
