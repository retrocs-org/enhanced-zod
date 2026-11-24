<?php
declare(strict_types=1);
/** Shared public documentation; never contains credentials or operator keys. */
function api_catalog(): array {
    return [
        ['GET','/healthz','Public','Checks that the database schema is available.',true],
        ['GET','/api/servers','Public','Active public servers, lobby state, player counts and connection addresses.',true],
        ['GET','/matches','Public','Compatibility alias for the public server directory.',true],
        ['GET','/api/leaderboard','Public','All-time public commander records, ordered by wins and kills.',true],
        ['GET','/api/statistics','Public','Network totals, popular maps and recent public battles.',true],
        ['GET','/api/catalog','Public','This endpoint catalog: method, path, access and description.',true],
        ['GET','/api/me','Account session','Returns the signed-in account. Use the account page to sign in.',false],
        ['POST','/api/accounts','Public; rate limited','Creates an account with username and password fields.',false],
        ['POST','/api/login','Public; rate limited','Signs in with username and password; issues a session cookie.',false],
        ['POST','/api/logout','Account session','Revokes the current session.',false],
        ['POST','/api/account/password','Account session','Changes the password with current_password and password fields.',false],
        ['POST','/api/tickets','Account session','Creates a single-use ticket for the server identifier.',false],
        ['POST','/api/tickets/redeem','Server bearer key','Redeems ticket for the authenticated dedicated server.',false],
        ['POST','/api/servers/heartbeat','Server bearer key','Updates the approved server listing and live lobby state.',false],
        ['DELETE','/api/servers/self','Server bearer key','Removes the authenticated server from the directory.',false],
        ['POST','/api/results','Server bearer key','Records a completed public match and authenticated participant statistics.',false],
    ];
}
