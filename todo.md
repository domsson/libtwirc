# Documentation

## Login data

- Will the fields be NULL if not set?
  - Relevant for user/pass in case of anon connection
  - Relevant for name/id if not (yet) receieved

## Transient nature of data

- Event info reveived in callbacks will be free'd once callback returns
  - This means user has to duplicate/copy data to store/use them after

## Change in interface

- `twirc_login` became `twirc_get_login`
- `twirc_last_error` became `twirc_get_last_error`
- `twirc_tag_by_key` became `twirc_get_tag_by_key`
  - Also, it now returns the `*twirc_tag_t` instead of just the value!

## Anonymous connection

- You don't _need_ login data; you can connect anonymously
  - This needs to be clarified in the section _Simple Example_
  - This should be explained in a new section, _Connecting_


# Features and bugfixes (required)

- Consistently set the error code, for example for 'Out of memory'


# Features and bugfixes (optional)

- Implement Room support
- Implement SSL/HTTS/whatever support

