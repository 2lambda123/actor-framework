*** Settings ***
Documentation       A test suite for the http client functionality

Library             Process
Library             String

Suite Setup         Initialize LP Chat Server
Suite Teardown      Terminate LP Chat Server


*** Variables ***
${SERVER_HOST}      localhost
${SERVER_PORT}      55519
${SERVER_PATH}      /path/to/the/server
${CLIENT_PATH}      /path/to/the/client

${INPUT}            Hello\nToday is a wonderful day!\n
${BASELINE}         bob: Hello\nbob: Today is a wonderful day!


*** Test Cases ***
Send Some message
    Start Process    ${CLIENT_PATH}    -p    ${SERVER_PORT}    -n    listening    alias=listening
    Run Process    ${CLIENT_PATH}    -p    ${SERVER_PORT}    -n    bob    stdin=${INPUT}
    Sleep    100ms    Wait for write to stdout
    ${result}=    Terminate Process    listening
    Should Be Equal As Strings    ${result.stdout}    ${BASELINE}


*** Keywords ***
Initialize LP Chat Server
    Start Process    ${SERVER_PATH}    -p    ${SERVER_PORT}    alias=chat-server

Terminate LP Chat Server
    Terminate Process    chat-server
