# twirc

twirc will be a minimal Twitch IRC library written in C. There will be no support for IRC features that aren't part of Twitch's IRC implementation. On the other hand, some IRCv3 features, like `CAP REQ` will be implemented in order to be compatible with Twitch IRC. This is currently a work in progress, and a pretty early one. Don't even try to use this yet.

Part of the development currently happens live on Twitch: [twitch.tv/domsson](https://twitch.tv/domsson)


# Development Notes

- **THIS NEEDS UNIT TESTS, DON'T BE LAZY DOMSSON**
- Question: what's the maximum amount of data the Twitch IRC servers will send in one go? 
- Possible answer: TCP max packet size is, according to some SO post, 64K. So that should definitely suffice.
- The Twitch IRC server often sends several messages in one go, so you have to split the received data on the null terminator yourself, a simple strtok() won't work
- Maximum message length seems to be 500 bytes (that is the message itself, no meta data), which means Twitch seems to abide by the IRCv3 protocol limitations
