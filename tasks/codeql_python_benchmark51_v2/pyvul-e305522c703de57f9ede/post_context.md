# Patch-overlapping Python context after the fix

## `keylime/registrar_common.py` — `class:UnprotectedHandler` (lines 204-477)

```python
class UnprotectedHandler(BaseHandler):
    def do_HEAD(self) -> None:
        """HEAD not supported"""
        web_util.echo_json_response(self, 405, "HEAD not supported")

    def do_PATCH(self) -> None:
        """PATCH not supported"""
        web_util.echo_json_response(self, 405, "PATCH not supported")

    def do_GET(self) -> None:
        """This method handles the GET requests to the unprotected side of the Registrar Server

        Currently the only supported path is /versions which shows the supported API versions
        """
        rest_params = web_util.get_restful_params(self.path)
        if rest_params is None:
            web_util.echo_json_response(self, 405, "Not Implemented: Use /version/ interface")
            return

        if "version" not in rest_params:
            web_util.echo_json_response(self, 400, "URI not supported")
            logger.warning("GET agent returning 400 response. URI not supported: %s", self.path)
            return

        version_info = {
            "current_version": keylime_api_version.current_version(),
            "supported_versions": keylime_api_version.all_versions(),
        }

        web_util.echo_json_response(self, 200, "Success", version_info)

    @staticmethod
    def get_network_params(
        json_body: Dict[str, Any], agent_id: str
    ) -> Tuple[Optional[str], Optional[int], Optional[str]]:
        # Validate ip and port
        ip = json_body.get("ip")
        if ip is not None:
            try:
                ipaddress.ip_address(ip)
            except ValueError:
                logger.warning("Contact ip for agent %s is not a valid ip got: %s.", agent_id, ip)
                ip = None

        port = json_body.get("port")
        if port is not None:
            try:
                port = int(port)
                if port < 1 or port > 65535:
                    logger.warning(
                        "Contact port for agent %s is not a number between 1 and 65535 got: %s.", agent_id, port
                    )
                    port = None
            except ValueError:
                logger.warning("Contact port for agent %s is not a valid number got: %s.", agent_id, port)
                port = None

        mtls_cert = json_body.get("mtls_cert")
        if mtls_cert is None or mtls_cert == "disabled":
            logger.warning("Agent %s did not send a mTLS certificate. Most operations will not work!", agent_id)

        return ip, port, mtls_cert

    def do_POST(self) -> None:
        """This method handles the POST requests to add agents to the Registrar Server.

        Currently, only agents resources are available for POSTing, i.e. /agents. All other POST uri's
        will return errors. POST requests require an an agent_id identifying the agent to add, and json
        block sent in the body with 2 entries: ek and aik.
        """
        session = SessionManager().make_session(engine)

        _, agent_id = self._validate_input("POST", True)
        if not agent_id:
            return

        try:
            content_length = int(self.headers.get("Content-Length", 0))
            if content_length == 0:
                web_util.echo_json_response(self, 400, "Expected non zero content length")
                logger.warning("POST for %s returning 400 response. Expected non zero content length.", agent_id)
                return

            post_body = self.rfile.read(content_length)
            json_body = json.loads(post_body)

            ekcert = json_body["ekcert"]
            aik_tpm = json_body["aik_tpm"]

            if ekcert is None or ekcert == "emulator":
                logger.warning("Agent %s did not submit an ekcert", agent_id)
                ek_tpm = json_body["ek_tpm"]
            else:
                if "ek_tpm" in json_body:
                    # This would mean the agent submitted both a non-None ekcert, *and*
                    #  an ek_tpm... We can deal with it by just ignoring the ek_tpm they sent
                    logger.warning("Overriding ek_tpm for agent %s from ekcert", agent_id)
                # If there's an EKCert, we just overwrite their ek_tpm
                # Note, we don't validate the EKCert here, other than the implicit
                #  "is it a valid x509 cert" check. So it's still untrusted.
                # This will be validated by the tenant.
                cert = cert_utils.x509_der_cert(base64.b64decode(ekcert))
                pubkey = cert.public_key()
                assert isinstance(pubkey, (rsa.RSAPublicKey, ec.EllipticCurvePublicKey))
                ek_tpm = base64.b64encode(tpm2_objects.ek_low_tpm2b_public_from_pubkey(pubkey)).decode()

            aik_attrs = tpm2_objects.get_tpm2b_public_object_attributes(
                base64.b64decode(aik_tpm),
            )
            if aik_attrs != tpm2_objects.AK_EXPECTED_ATTRS:
                web_util.echo_json_response(self, 400, "Invalid AK attributes")
                logger.warning(
                    "Agent %s submitted AIK with invalid attributes! %s (provided) != %s (expected)",
                    agent_id,
                    tpm2_objects.object_attributes_description(aik_attrs),
                    tpm2_objects.object_attributes_description(tpm2_objects.AK_EXPECTED_ATTRS),
                )
                return

            # try to encrypt the AIK
            aik_enc = Tpm.encryptAIK(
                agent_id,
                base64.b64decode(ek_tpm),
                base64.b64decode(aik_tpm),
            )
            if aik_enc is None:
                logger.warning("Agent %s failed encrypting AIK", agent_id)
                web_util.echo_json_response(self, 400, "Error: failed encrypting AK")
                return

            blob, key = aik_enc

            # special behavior if we've registered this uuid before
            regcount = 1
            try:
                agent = session.query(RegistrarMain).filter_by(agent_id=agent_id).first()
            except NoResultFound:
                agent = None
            except SQLAlchemyError as e:
                logger.error("SQLAlchemy Error: %s", e)
                raise

            if agent is not None:
                # keep track of how many ek-ekcerts have registered on this uuid
                assert isinstance(agent.regcount, int)
                regcount = agent.regcount
                if agent.ek_tpm != ek_tpm or agent.ekcert != ekcert:  # pyright: ignore
                    logger.warning("WARNING: Overwriting previous registration for this UUID with new ek-ekcert pair!")
                    regcount += 1

                # force overwrite
                logger.info("Overwriting previous registration for this UUID.")
                try:
                    session.query(RegistrarMain).filter_by(agent_id=agent_id).delete()
                    session.commit()
                except SQLAlchemyError as e:
                    logger.error("SQLAlchemy Error: %s", e)
                    raise

            # Check for ip and port and mTLS cert
            contact_ip, contact_port, mtls_cert = UnprotectedHandler.get_network_params(json_body, agent_id)

            # Add values to database
            d: Dict[str, Any] = {
                "agent_id": agent_id,
                "ek_tpm": ek_tpm,
                "aik_tpm": aik_tpm,
                "ekcert": ekcert,
                "ip": contact_ip,
                "mtls_cert": mtls_cert,
                "port": contact_port,
                "virtual": int(ekcert == "virtual"),
                "active": int(False),
                "key": key,
                "provider_keys": {},
                "regcount": regcount,
            }

            try:
                session.add(RegistrarMain(**d))
                session.commit()
            except SQLAlchemyError as e:
                logger.error("SQLAlchemy Error: %s", e)
                raise

            if rmc:
                try:
                    rmc.record_create(d, None, None)

                except Exception as e:
                    logger.error("Durable Attestation Error: %s", e)
                    raise

            response = {
                "blob": blob,
            }
            web_util.echo_json_response(self, 200, "Success", response)

            logger.info("POST returning key blob for agent_id: %s", agent_id)
        except Exception as e:
            web_util.echo_json_response(self, 400, f"Error: {str(e)}")
            logger.warning("POST for %s returning 400 response. Error: %s", agent_id, e)
            logger.exception(e)

    def do_PUT(self) -> None:
        """This method handles the PUT requests to add agents to the Registrar Server.

        Currently, only agents resources are available for PUTing, i.e. /agents. All other PUT uri's
        will return errors.
        """
        session = SessionManager().make_session(engine)

        _, agent_id = self._validate_input("PUT", True)
        if not agent_id:
            return

        try:
            content_length = int(self.headers.get("Content-Length", 0))
            if content_length == 0:
                web_util.echo_json_response(self, 400, "Expected non zero content length")
                logger.warning("PUT for %s returning 400 response. Expected non zero content length.", agent_id)
                return

            post_body = self.rfile.read(content_length)
            json_body = json.loads(post_body)

            auth_tag = json_body["auth_tag"]
            try:
                agent = session.query(RegistrarMain).filter_by(agent_id=agent_id).first()
            except NoResultFound as e:
                raise Exception("attempting to activate agent before requesting " f"registrar for {agent_id}") from e
            except SQLAlchemyError as e:
                logger.error("SQLAlchemy Error: %s", e)
                raise

            assert agent
            assert isinstance(agent.key, str)
            ex_mac = crypto.do_hmac(agent.key.encode(), agent_id)
            if ex_mac == auth_tag:
                try:
                    session.query(RegistrarMain).filter(RegistrarMain.agent_id == agent_id).update(
                        {"active": int(True)}
                    )
                    session.commit()
                except SQLAlchemyError as e:
                    logger.error("SQLAlchemy Error: %s", e)
                    raise
            else:
                if agent_id and session.query(RegistrarMain).filter_by(agent_id=agent_id).delete():
                    try:
                        session.commit()
                    except SQLAlchemyError as e:
                        logger.error("SQLAlchemy Error: %s", e)
                        raise

                raise Exception(
                    f"Auth tag {auth_tag} for agent {agent_id} does not match expected value. The agent has been deleted from database, and a restart of it will be required"
                )

            web_util.echo_json_response(self, 200, "Success")
            logger.info("PUT activated: %s", agent_id)
        except Exception as e:
            web_util.echo_json_response(self, 400, f"Error: {str(e)}")
            logger.warning("PUT for %s returning 400 response. Error: %s", agent_id, e)
            logger.exception(e)
            return

    def do_DELETE(self) -> None:
        """DELETE not supported"""
        web_util.echo_json_response(self, 405, "DELETE not supported")

    # pylint: disable=W0622
    def log_message(self, format: str, *args: Any) -> None:
        return
```

## `keylime/registrar_common.py` — `function:UnprotectedHandler.get_network_params` (lines 235-265)

```python
    def get_network_params(
        json_body: Dict[str, Any], agent_id: str
    ) -> Tuple[Optional[str], Optional[int], Optional[str]]:
        # Validate ip and port
        ip = json_body.get("ip")
        if ip is not None:
            try:
                ipaddress.ip_address(ip)
            except ValueError:
                logger.warning("Contact ip for agent %s is not a valid ip got: %s.", agent_id, ip)
                ip = None

        port = json_body.get("port")
        if port is not None:
            try:
                port = int(port)
                if port < 1 or port > 65535:
                    logger.warning(
                        "Contact port for agent %s is not a number between 1 and 65535 got: %s.", agent_id, port
                    )
                    port = None
            except ValueError:
                logger.warning("Contact port for agent %s is not a valid number got: %s.", agent_id, port)
                port = None

        mtls_cert = json_body.get("mtls_cert")
        if mtls_cert is None or mtls_cert == "disabled":
            logger.warning("Agent %s did not send a mTLS certificate. Most operations will not work!", agent_id)

        return ip, port, mtls_cert
```

## `keylime/registrar_common.py` — `function:UnprotectedHandler.do_PUT` (lines 408-469)

```python
    def do_PUT(self) -> None:
        """This method handles the PUT requests to add agents to the Registrar Server.

        Currently, only agents resources are available for PUTing, i.e. /agents. All other PUT uri's
        will return errors.
        """
        session = SessionManager().make_session(engine)

        _, agent_id = self._validate_input("PUT", True)
        if not agent_id:
            return

        try:
            content_length = int(self.headers.get("Content-Length", 0))
            if content_length == 0:
                web_util.echo_json_response(self, 400, "Expected non zero content length")
                logger.warning("PUT for %s returning 400 response. Expected non zero content length.", agent_id)
                return

            post_body = self.rfile.read(content_length)
            json_body = json.loads(post_body)

            auth_tag = json_body["auth_tag"]
            try:
                agent = session.query(RegistrarMain).filter_by(agent_id=agent_id).first()
            except NoResultFound as e:
                raise Exception("attempting to activate agent before requesting " f"registrar for {agent_id}") from e
            except SQLAlchemyError as e:
                logger.error("SQLAlchemy Error: %s", e)
                raise

            assert agent
            assert isinstance(agent.key, str)
            ex_mac = crypto.do_hmac(agent.key.encode(), agent_id)
            if ex_mac == auth_tag:
                try:
                    session.query(RegistrarMain).filter(RegistrarMain.agent_id == agent_id).update(
                        {"active": int(True)}
                    )
                    session.commit()
                except SQLAlchemyError as e:
                    logger.error("SQLAlchemy Error: %s", e)
                    raise
            else:
                if agent_id and session.query(RegistrarMain).filter_by(agent_id=agent_id).delete():
                    try:
                        session.commit()
                    except SQLAlchemyError as e:
                        logger.error("SQLAlchemy Error: %s", e)
                        raise

                raise Exception(
                    f"Auth tag {auth_tag} for agent {agent_id} does not match expected value. The agent has been deleted from database, and a restart of it will be required"
                )

            web_util.echo_json_response(self, 200, "Success")
            logger.info("PUT activated: %s", agent_id)
        except Exception as e:
            web_util.echo_json_response(self, 400, f"Error: {str(e)}")
            logger.warning("PUT for %s returning 400 response. Error: %s", agent_id, e)
            logger.exception(e)
            return
```
