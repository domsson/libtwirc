# twirc

twirc will be a minimal Twitch IRC library written in C. There will be no support for IRC features that aren't part of Twitch's IRC implementation. On the other hand, some IRCv3 features, like `CAP REQ` will be implemented in order to be compatible with Twitch IRC. This is currently a work in progress, and a pretty early one. Don't even try to use this yet.

Part of the development currently happens live on Twitch: [twitch.tv/domsson](https://twitch.tv/domsson)

# Motivation

I wanted to write a Twitch chat bot in C. I found `libircclient` and was using it happily, but ran into two issues. First, it doesn't support IRCv3 tags, which Twitch is using. Second, it uses a GPL license. Now, my bot (and almost all my software) is CC0 (aka public domain) and even after more than 4 hours of research, I couldn't figure out if I would be able to release my code as CC0 when using a GPL licensend library. This, plus the fact that I'm still learning C and looking for small projects to help me gain more experience, I decided to write my own IRC library. To keep the scope smaller, I decided to make it Twitch-specific and, for now, Linux only. 

# Development Notes

- **THIS NEEDS UNIT TESTS, DON'T BE LAZY DOMSSON**
- Question: what's the maximum amount of data the Twitch IRC servers will send in one go? 
- Possible answer: TCP max packet size is, according to some SO post, 64K. So that should definitely suffice. However, a smaller buffer should still be fine as we can just call recv() over and over until we've processed all the data that is waiting. So I'm still undecided on the buffer size.
- The Twitch IRC server often sends several messages in one go, so you have to split the received data on the null terminator yourself, a simple strtok() won't work
- Also see [this article on the issues of receiving network data](https://faq.cprogramming.com/cgi-bin/smartfaq.cgi?id=1044780608&answer=1108255660) for how received data can be split up pretty randomly, which means we need to program against this and assemble everything on our side 
- Maximum message length seems to be 500 bytes (that is the actual, visible chat message itself, excluding tags, command name and prefix), which means Twitch seems to abide by the IRCv3 protocol limitations
- Tests have shown that while Twitch DOES seem to respect the limitation on the actual message length (visible chat message), it does NOT respect the maximum lengths limitations as set forth by the IRC and IRCv3 specifications in general: according to some tests, only the visible chat message seems to be limited; the tags can easily exceed the 512 byte limited if many emotes are being used (as they are listed in the tags), plus the prefix and command strings are also not counted against the message length limited and can therefore increase it further
- Summarizing the last point, 1024 bytes is NOT enough for a Twitch IRC message; 2048 seems to be sufficient according to some tests
- We've got to figure out if we need to support CTCP (`/me`, for example) or if Twitch IRC is handling this differently
