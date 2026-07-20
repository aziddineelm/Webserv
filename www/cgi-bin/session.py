#!/usr/bin/env python3
import os
import time
import hashlib
import json

# Read environment
method = os.environ.get('REQUEST_METHOD', 'GET')
cookie_header = os.environ.get('HTTP_COOKIE', '')
query_string = os.environ.get('QUERY_STRING', '')

# Simple in-memory action from query string
# e.g. ?action=set&name=user&value=John
params = {}
for part in query_string.split('&'):
    if '=' in part:
        k, v = part.split('=', 1)
        params[k] = v

action = params.get('action', 'status')

# Parse existing cookies
cookies = {}
if cookie_header:
    for item in cookie_header.split(';'):
        item = item.strip()
        if '=' in item:
            k, v = item.split('=', 1)
            cookies[k.strip()] = v.strip()

# Build response
response_cookies = []
result = {}

if action == 'set':
    name = params.get('name', 'test_cookie')
    value = params.get('value', 'hello')
    max_age = params.get('max_age', '3600')
    response_cookies.append(
        'Set-Cookie: {}={}; Path=/; Max-Age={}'.format(name, value, max_age)
    )
    result = {
        'action': 'set',
        'cookie': name,
        'value': value,
        'max_age': max_age,
        'message': 'Cookie "{}" set successfully'.format(name)
    }

elif action == 'delete':
    name = params.get('name', 'test_cookie')
    response_cookies.append(
        'Set-Cookie: {}=deleted; Path=/; Max-Age=0'.format(name)
    )
    result = {
        'action': 'delete',
        'cookie': name,
        'message': 'Cookie "{}" deleted'.format(name)
    }

elif action == 'session':
    session_id = cookies.get('session_id', '')
    if not session_id:
        # Create new session
        session_id = hashlib.sha256(
            (str(time.time()) + os.environ.get('REMOTE_ADDR', '')).encode()
        ).hexdigest()[:32]
        response_cookies.append(
            'Set-Cookie: session_id={}; Path=/; HttpOnly'.format(session_id)
        )
        result = {
            'action': 'session',
            'session_id': session_id,
            'is_new': True,
            'message': 'New session created'
        }
    else:
        result = {
            'action': 'session',
            'session_id': session_id,
            'is_new': False,
            'message': 'Existing session found'
        }

elif action == 'destroy':
    response_cookies.append(
        'Set-Cookie: session_id=deleted; Path=/; Max-Age=0'
    )
    result = {
        'action': 'destroy',
        'message': 'Session destroyed'
    }

else:
    # status — list all cookies
    result = {
        'action': 'status',
        'cookies': cookies,
        'cookie_count': len(cookies),
        'message': '{} cookie(s) found'.format(len(cookies))
    }

# Output headers
print('Content-Type: application/json')
for sc in response_cookies:
    print(sc)
print()

# Output body
print(json.dumps(result, indent=2))
