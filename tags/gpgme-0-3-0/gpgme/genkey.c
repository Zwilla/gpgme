/* genkey.c -  key generation
 *	Copyright (C) 2000 Werner Koch (dd9jn)
 *      Copyright (C) 2001 g10 Code GmbH
 *
 * This file is part of GPGME.
 *
 * GPGME is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GPGME is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "util.h"
#include "context.h"
#include "ops.h"

static void
genkey_status_handler ( GpgmeCtx ctx, GpgStatusCode code, char *args )
{
    if ( code == STATUS_PROGRESS && *args ) {
        if (ctx->progress_cb) {
            char *p;
            int type=0, current=0, total=0;
            
            if ( (p = strchr (args, ' ')) ) {
                *p++ = 0;
                if (*p) {
                    type = *(byte*)p;
                    if ( (p = strchr (p+1, ' ')) ) {
                        *p++ = 0;
                        if (*p) {
                            current = atoi (p);
                            if ( (p = strchr (p+1, ' ')) ) {
                                *p++ = 0;
                                total = atoi (p);
                            }
                        }
                    }
                }
            }           
            if ( type != 'X' )
                ctx->progress_cb ( ctx->progress_cb_value, args, type,
                                   current, total );
        }
        return;
    }

    DEBUG2 ("genkey_status: code=%d args=`%s'\n", code, args );
    /* FIXME: Need to do more */
}


/**
 * gpgme_op_genkey:
 * @c: the context
 * @parms: XML string with the key parameters
 * @pubkey: Returns the public key
 * @seckey: Returns the secret key
 * 
 * Generate a new key and store the key in the default keyrings if
 * both @pubkey and @seckey are NULL.  If @pubkey and @seckey are
 * given, the newly created key will be returned in these data
 * objects.  This function just starts the gheneration and does not
 * wait for completion.
 *
 * Here is an example on how @parms should be formatted; for deatils
 * see the file doc/DETAILS from the GnuPG distribution.
 *
 * <literal>
 * <![CDATA[
 * <GnupgKeyParms format="internal">
 * Key-Type: DSA
 * Key-Length: 1024
 * Subkey-Type: ELG-E
 * Subkey-Length: 1024
 * Name-Real: Joe Tester
 * Name-Comment: with stupid passphrase
 * Name-Email: joe@foo.bar
 * Expire-Date: 0
 * Passphrase: abc
 * </GnupgKeyParms>
 * ]]>
 * </literal> 
 *
 * Strings should be given in UTF-8 encoding.  The format we support
 * for now is only "internal".  The content of the
 * &lt;GnupgKeyParms&gt; container is passed verbatim to GnuPG.
 * Control statements are not allowed.
 * 
 * Return value: 0 for success or an error code
 **/
GpgmeError
gpgme_op_genkey_start (GpgmeCtx ctx, const char *parms,
		       GpgmeData pubkey, GpgmeData seckey)
{
  int err = 0;
  const char *s, *s2, *sx;

  fail_on_pending_request (ctx);
  ctx->pending = 1;

  gpgme_data_release (ctx->help_data_1);
  ctx->help_data_1 = NULL;

  _gpgme_engine_release (ctx->engine);
  ctx->engine = NULL;
  err = _gpgme_engine_new (ctx->use_cms ? GPGME_PROTOCOL_CMS
			   : GPGME_PROTOCOL_OpenPGP, &ctx->engine);
  if (err)
    goto leave;

  /* We need a special mechanism to get the fd of a pipe here, so
   * that we can use this for the %pubring and %secring parameters.
   * We don't have this yet, so we implement only the adding to the
   * standard keyrings */
  if (pubkey || seckey)
    {
      err = mk_error (Not_Implemented);
      goto leave;
    }

  if (!pubkey && !seckey)
    ; /* okay: Add key to the keyrings */
  else if (!pubkey || gpgme_data_get_type (pubkey) != GPGME_DATA_TYPE_NONE)
    {
      err = mk_error (Invalid_Value);
      goto leave;
    }
  else if (!seckey || gpgme_data_get_type (seckey) != GPGME_DATA_TYPE_NONE)
    {
      err = mk_error (Invalid_Value);
      goto leave;
    }
    
  if (pubkey)
    {
      _gpgme_data_set_mode (pubkey, GPGME_DATA_MODE_IN);
      _gpgme_data_set_mode (seckey, GPGME_DATA_MODE_IN);
      /* FIXME: Need some more things here.  */
    }

  if ((parms = strstr (parms, "<GnupgKeyParms ")) 
      && (s = strchr (parms, '>'))
      && (sx = strstr (parms, "format=\"internal\""))
      && sx < s
      && (s2 = strstr (s+1, "</GnupgKeyParms>")))
    {
      /* FIXME: Check that there are no control statements inside.  */
      err = gpgme_data_new_from_mem (&ctx->help_data_1, s+1, s2-s-1, 1);
    }
  else 
    err = mk_error (Invalid_Value);

  if (err)
    goto leave;
    
  _gpgme_data_set_mode (ctx->help_data_1, GPGME_DATA_MODE_OUT);

  _gpgme_engine_set_status_handler (ctx->engine, genkey_status_handler, ctx);
  _gpgme_engine_set_verbosity (ctx->engine, ctx->verbosity);

  err = _gpgme_engine_op_genkey (ctx->engine, ctx->help_data_1, ctx->use_armor);

  if (!err)
    err = _gpgme_engine_start (ctx->engine, ctx);

 leave:
  if (err)
    {
      ctx->pending = 0; 
      _gpgme_engine_release (ctx->engine);
      ctx->engine = NULL;
    }
  return err;
}

/**
 * gpgme_op_genkey:
 * @c: the context
 * @parms: XML string with the key parameters
 * @pubkey: Returns the public key
 * @seckey: Returns the secret key
 * 
 * Generate a new key and store the key in the default keyrings if both
 * @pubkey and @seckey are NULL.  If @pubkey and @seckey are given, the newly
 * created key will be returned in these data objects.
 * See gpgme_op_genkey_start() for a description of @parms.
 * 
 * Return value: 0 for success or an error code
 **/
GpgmeError
gpgme_op_genkey( GpgmeCtx c, const char *parms,
                 GpgmeData pubkey, GpgmeData seckey )
{
    int rc = gpgme_op_genkey_start ( c, parms, pubkey, seckey );
    if ( !rc ) {
        gpgme_wait (c, 1);
        c->pending = 0;
    }
    return rc;
}




