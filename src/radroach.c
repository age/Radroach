/* Radroach is a simple IRC bot
   Copyright © 2009  Evgeny Grablyk <evgeny.grablyk@gmail.com>

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

#include <radroach.h>
#include <plugins.c>
#include <util.c>

#define BUFSZ 10

char inbuf[BUFSZ];
/* Refers to global configuration structure. */
settings_t *settings = NULL;

void setup (void);

/* Returns a line from irc server. */
/* FIXME: store everything in a buffer, give out lines */
char *
sogetline (int s)
{
  char c = 0, *buf, *rebuf;
  int r, i = 0, bsz = BUFSZ;

  buf = malloc (BUFSZ);
  while ((r = read (s, &c, 1)) != -1)
    {
      /* ordinary read */
      if (r)
	buf[i++] = c;
      /* all done, reallocate and return */
      /* string is less than our buffer, reallocate. */
      if (c == '\n' && i < bsz)
	{
	  buf[i] = '\0';
	  rebuf = malloc (strlen (buf) + 1);
	  strcpy (rebuf, buf);
	  free (buf);
	  return rebuf;
	}
      else if (c == '\n' && i == bsz)
	{
	  buf[i - 1] = '\0';
	  return buf;
	}
      /* we've reached buffer limit, reallocate with more space */
      if (i >= bsz)
	{
	  rebuf = malloc (i + BUFSZ);
	  memcpy (rebuf, buf, i);
	  free (buf);
	  buf = rebuf;
	  bsz += BUFSZ;
	}
    }
  /* something failed and we did not return a sting earlier */
  return NULL;

}

/* free(), but for message struct. */
void
msgfree (message_t * msg)
{
  free (msg->raw);
  free (msg);
}

/* Perform connection to server. */
void
sconnect (char *host)
{
  struct addrinfo *server = NULL, hints;
  int st, sock;

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  if ((sock = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      error (0, 0, "Socket creation failed.\n");
      goto err;
    }
  if ((st = getaddrinfo (host, "ircd", &hints, &server)) != 0)
    {
      error (0, 0, "Failed to resolve hostname: %s", gai_strerror (st));
      goto err;
    }
  logstr ("Hostname resolved.\n");
  if (connect (sock, server->ai_addr, server->ai_addrlen) == -1)
    {
      error (0, 0, "Connection to server failed: %s", gai_strerror (errno));
      goto err;
    }
  logstr ("Connected.\n");
  settings->sock = sock;
  freeaddrinfo (server);

  /* inbuf is not used yet, don't scream. */
  sprintf (inbuf, "NICK %s\n", settings->nick);
  raw (inbuf);
  setup ();
  return;

err:
  if (server)
    freeaddrinfo (server);
  exit (EXIT_FAILURE);
}

/* Performs after-connection actions. */
void
setup (void)
{
  char *l = NULL;
  if (settings->name)
    {
      l =
	malloc (strlen ("USER  * * :\n") + strlen (settings->name) +
		strlen (settings->nick) + 1);
      sprintf (l, "USER %s * * :%s\n", settings->nick,
	       settings->name);
      raw (l);
    }
  else
    {
      l =
	malloc (strlen ("USER  localhost  :CBot - a bot in C, by age\n") +
		strlen (settings->nick) + strlen (settings->host) + 1);
      sprintf (l, "USER %s localhost %s :CBot - a bot in C, by age\n",
	       settings->nick, settings->host);
      raw (l);
    }
  free (l);
}

/*  Parses a string, returns message containing that string's data. */
message_t *
parsemsg (char *l)
{
  /* we only want to parse private messages, which may contain instructions */
  if (strstr (l, "PRIVMSG") != NULL && l[0] == ':')
    {
      message_t *result = malloc (sizeof (message_t));
      result->raw = l;

      result->sender = &l[1];
      l = strchr (l, '!') + 1;
      *(l - 1) = '\0';

      result->ident = &l[0];
      l = strchr (l, '@') + 1;
      *(l - 1) = '\0';

      result->host = &l[0];
      l = strchr (l, ' ') + 1;
      *(l - 1) = '\0';

      /* we need to skip "PRIVMSG" */
      l = strchr (l, ' ') + 1;
      result->dest = &l[0];
      /* skipping ":" */
      l = strchr (l, ' ') + 2;
      *(l - 2) = '\0';

      /* string ends with \r\n, we want just \0 */
      result->msg = &l[0];
      l = strchr (l, '\r');
      *l = '\0';

      return result;
    }
  return NULL;
}

/* Parses a string, returns command containing string's data. */
command_t *
parsecmd (char *l)
{
  if (l[0] == settings->action_trigger && l[1] != ' ')
    {
      command_t *result = malloc (sizeof (command_t));
      result->action = &l[1];

      l = strchr (l, ' ');	/* if there is a space after command name, we may expect parameters */
      if (l != NULL)
	{
	  *l = '\0';
	  l++;
	  result->params = (*l != '\0') ? l : NULL;
	  return result;
	}
      else			/* there are no parameters at all */
	{
	  result->params = NULL;
	  return result;
	}
    }
  return NULL;
}

/* Checks if there's a ping request, replies if there is. Additionally calls setup(). */
int
p_response (char *l)
{
  if (strstr (l, "PING ") != NULL)
    {
      memcpy (l, "PONG", 4);
      raw (l);
      free (l);
      return 1;
    }
  return 0;
}

/* Parses a settingsiguration file. */
int
parsecfg (char *cfile)
{
  cfg_t *cfg = NULL;
  int status;

  cfg_opt_t opts[] = {
    CFG_SIMPLE_STR ("nick", &settings->nick),
    CFG_SIMPLE_STR ("name", &settings->name),
    CFG_SIMPLE_STR ("host", &settings->host),
    CFG_SIMPLE_STR ("trusted", &settings->trusted),
    CFG_SIMPLE_STR ("password", &settings->password),
    CFG_END ()
  };

  /* default values */
  settings->nick = NULL;
  settings->name = NULL;
  settings->host = NULL;
  settings->trusted = NULL;
  settings->password = NULL;

  cfg = cfg_init (opts, 0);
  status = cfg_parse (cfg, cfile);
  printf ("%s: Trusted users are: %s\n", settings->execname, settings->trusted);
  /* check for necessary settings */
  if (status == CFG_SUCCESS && settings->nick != NULL && settings->host != NULL)
    return 1;
  return 0;
}

/* Checks if message is from a trusted source. */
int
checkrights (message_t * msg)
{
  char *l, *s;
  /* just fix me alrady */
  return 1;
  
  s = malloc (2 + strlen (msg->ident) + strlen (msg->host));
  sprintf (s, "%s@%s", msg->ident, msg->host);
  for (l = strtok (settings->trusted, " "); l != NULL; l = strtok (NULL, " "))
    if (strstr (s, l))
      {
	free (s);
	return 1;
      }
  free (s);
  return 0;
}

/* Executes apropriate function as parameters specify. */
void
execute (message_t * msg, command_t * cmd)
{
  plugin_t *a;

  a = plugin_find (cmd->action);
  if (checkrights (msg) && a != NULL)
    {
      printf ("%s: Accepted command from %s (%s@%s)\n", settings->execname,
	      msg->sender, msg->ident, msg->host);
      printf ("%s: Executing command '%s' with parameters: '%s'\n",
	      settings->execname, a->name, cmd->params);
      a->execute (msg, cmd);
    }
  else if (a == NULL)
    {
      printf
	("%s: No handler found for this command: '%s' (supplied parameters: '%s')\n",
	 settings->execname, cmd->action, cmd->params);
    }
  else
    printf
      ("%s: Untrusted user %s (%s@%s) tried to execute command %s, ignored\n",
       settings->execname, msg->sender, msg->ident, msg->host, cmd->action);

  free (cmd);
  msgfree (msg);
}

/* Parses commandline arguments. */
int
configure (int argc, char *argv[])
{
  char *cfile = NULL;
  int opt;
  settings = (settings_t *) malloc (sizeof (settings_t));

  if (argc < 2)
    {
      p_help ();
      return 0;
    }

  settings->execname = argv[0];

  while ((opt = getopt (argc, argv, "hc:")) != -1)
    {
      switch (opt)
	{
	case 'h':
	  {
	    p_help ();
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

  if (cfile == NULL)
    {
      error (0, 0, "No confguration file specified.");
      return 0;
    }

  printf ("%s: Configuration file selected: %s\n", settings->execname, cfile);

  if (!parsecfg (cfile))
    {
      error (0, 0,
	     "You did not specify nickname and host or parsing settingsiguration failed.");
      return 0;
    }
  return 1;
}

int
main (int argc, char *argv[])
{
  char *l = NULL;
  message_t *cmsg = NULL;
  command_t *ccmd = NULL;

  printf ("%s: This is Radroach commit %s\n", argv[0], COMMIT);
  if (!configure (argc, argv))
    exit (EXIT_FAILURE);
  settings->action_trigger = '`';	/* default trigger char */
  plugins_init ("./plugins/");
  sconnect (settings->host);

  while ((l = sogetline (settings->sock)) != NULL)
    {
      if (p_response (l))
	continue;
      cmsg = parsemsg (l);
      if (cmsg != NULL)
	{
	  ccmd = parsecmd (cmsg->msg);
	  if (ccmd != NULL)
	    execute (cmsg, ccmd);
	}
    }
  return EXIT_SUCCESS;
}
