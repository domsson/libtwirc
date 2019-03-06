# libtwirc

`libtwirc` is a Twitch IRC client library written in C, developed on and for Linux. It allows you to easily implement chat bots or clients for Twitch with C or any language that can call into C libraries. The interface is pretty similar to that of `libircclient`.

`libtwirc` specifically implements the Twitch IRC flavor. This means that many features described in the IRC protocol are not supported, most notably `DCC` and SSL. On the other hand, IRCv3 tags, `CAP REQ`, `WHISPER` and other Twitch-specific commands are supported.

Part of the development happens live on Twitch: [twitch.tv/domsson](https://twitch.tv/domsson)

# Status

The library is feature-complete for the current (initial) release and seems to work well. However, I have not unit-tested it yet, so there most likely still bugs lurking. I would love some feedback from actual users, so why not give it a try?

# How to use

Check out the [wiki](https://github.com/domsson/libtwirc/wiki), which should have all the information to get you started. Also, there is some example code available over at [twircclient](https://github.com/domsson/twircclient). The rough and overly simplified outline of using `libtwirc` looks something like this:

```
twirc_state_t *s = twirc_init();        // create twirc state
twirc_connect(HOST, PORT, USER, TOKEN); // connect to Twitch IRC
twirc_loop();                           // run in a loop until disconnected
twirc_kill();                           // free resources
```

# Motivation

I wanted to write a Twitch chat bot in C. I found `libircclient` and was using it happily, but ran into two issues. First, it doesn't support IRCv3 tags, which Twitch is using. Second, it uses a GPL license. Now, my bot (and almost all my software) is CC0 (aka public domain) and even after more than 4 hours of research, I couldn't figure out if I would be able to release my code as CC0 when using a GPL licensend library. This, plus the fact that I'm still learning C and am looking for small projects to help me gain more experience, I decided to write my own IRC library. To keep the scope smaller, I decided to make it Twitch-specific and, for now, Linux only. 
