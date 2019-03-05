# libtwirc

`libtwirc` is a Twitch IRC client library written in C, developed on and for Linux. It allows you to easily implement chat bots or clients for Twitch with C or any language that can call into C libraries. The interface is pretty similar to that of `libircclient`.

`libtwirc` specifically implements the Twitch IRC flavor. This means that many features described in the IRC protocol are not supported, most notably `DCC` and SSL. On the other hand, IRCv3 tags, `CAP REQ`, `WHISPER` and other Twitch-specific commands are supported.

Part of the development happens live on Twitch: [twitch.tv/domsson](https://twitch.tv/domsson)

# Status

The library is pretty much feature-complete (at least for the initial release) and seems to work well. However, I have not unit-tested it yet, so I can't guarantee you won't see any segfaults at some point. Hence, I would love some feedback from actual users at this point! 

# How to use

I have yet to fill the wiki with documentation. Until then, you can check out [twircclient](https://github.com/domsson/twircclient) for some example code that makes use of `libtwirc`, including instructions on how to build. 

# Motivation

I wanted to write a Twitch chat bot in C. I found `libircclient` and was using it happily, but ran into two issues. First, it doesn't support IRCv3 tags, which Twitch is using. Second, it uses a GPL license. Now, my bot (and almost all my software) is CC0 (aka public domain) and even after more than 4 hours of research, I couldn't figure out if I would be able to release my code as CC0 when using a GPL licensend library. This, plus the fact that I'm still learning C and am looking for small projects to help me gain more experience, I decided to write my own IRC library. To keep the scope smaller, I decided to make it Twitch-specific and, for now, Linux only. 

# Development Notes

- **This needs unit tests, don't be lazy domsson!** [check](https://libcheck.github.io/check/web/install.html) looks quite nice.
- Question: what's the maximum amount of data the Twitch IRC servers will send in one go? 
- Possible answer: TCP max packet size is, according to some SO post, 64K. So that should definitely suffice. However, a smaller buffer should still be fine as we can just call recv() over and over until we've processed all the data that is waiting. So I'm still undecided on the buffer size.
- The Twitch IRC server often sends several messages in one go, so you have to split the received data on the null terminator yourself, a simple strtok() won't work
- Also see [this article on the issues of receiving network data](https://faq.cprogramming.com/cgi-bin/smartfaq.cgi?id=1044780608&answer=1108255660) for how received data can be split up pretty randomly, which means we need to program against this and assemble everything on our side 
- Maximum message length seems to be 500 bytes (that is the actual, visible chat message itself, excluding tags, command name and prefix), which means Twitch seems to abide by the IRCv3 protocol limitations
- Tests have shown that while Twitch DOES seem to respect the limitation on the actual message length (visible chat message), it does NOT respect the maximum lengths limitations as set forth by the IRC and IRCv3 specifications in general: according to some tests, only the visible chat message seems to be limited; the tags can easily exceed the 512 byte limited if many emotes are being used (as they are listed in the tags), plus the prefix and command strings are also not counted against the message length limited and can therefore increase it further
- Summarizing the last point, 1024 bytes is NOT enough for a Twitch IRC message; 2048 seems to be sufficient according to some tests
- We've got to figure out if we need to support CTCP (`/me`, for example) or if Twitch IRC is handling this differently
- Regarding CTCP, it seems Twitch is doing what you'd expect by adding a 0x01 byte at the beginning and end of CTCP messages (after the ':')
- It seems that `epoll_wait()` never returns more than one event, even if we give it the chance to; I assume this is because it's only watching a single file descriptor (socket) - hence, we can probably keep the code simple there
