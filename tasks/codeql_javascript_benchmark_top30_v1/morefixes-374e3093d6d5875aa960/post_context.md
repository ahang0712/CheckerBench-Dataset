# Patch-overlapping JavaScript context after the fix

## `js/libs/ui-shared/src/context/ErrorPage.tsx` (changed lines (10, 17, 19, 20, 31, 44, 45, 56))

```javascript
import {
  Button,
  Modal,
  ModalVariant,
  Page,
  Text,
  TextContent,
} from "@patternfly/react-core";
import { useTranslation } from "react-i18next";
import { getNetworkErrorMessage } from "../utils/errors";

type ErrorPageProps = {
  error?: unknown;
};

export const ErrorPage = (props: ErrorPageProps) => {
  const { t, i18n } = useTranslation();
  const error = props.error;
  const errorMessage = getErrorMessage(error);
  const networkErrorMessage = getNetworkErrorMessage(error);
  console.error(error);

  function onRetry() {
    location.href = location.origin + location.pathname;
  }

  return (
    <Page>
      <Modal
        variant={ModalVariant.small}
        title={t("somethingWentWrong")}
        titleIconVariant="danger"
        showClose={false}
        isOpen
        actions={[
          <Button key="tryAgain" variant="primary" onClick={onRetry}>
            {t("tryAgain")}
          </Button>,
        ]}
      >
        <TextContent>
          {errorMessage ? (
            <Text>{t(errorMessage)}</Text>
          ) : networkErrorMessage && i18n.exists(networkErrorMessage) ? (
            <Text>{t(networkErrorMessage)}</Text>
          ) : (
            <Text>{t("somethingWentWrongDescription")}</Text>
          )}
        </TextContent>
      </Modal>
    </Page>
  );
};

function getErrorMessage(error: unknown): string | null {
  if (error instanceof Error) {
    return error.message;
  }

  return null;
}
```

## `js/libs/ui-shared/src/context/KeycloakContext.tsx` (changed lines (70, 83, 84))

```javascript
import { Spinner } from "@patternfly/react-core";
import Keycloak from "keycloak-js";
import {
  PropsWithChildren,
  createContext,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { AlertProvider } from "../alerts/Alerts";
import { ErrorPage } from "./ErrorPage";
import { Help } from "./HelpContext";
import { BaseEnvironment } from "./environment";

export type KeycloakContext<T extends BaseEnvironment = BaseEnvironment> =
  KeycloakContextProps<T> & {
    keycloak: Keycloak;
  };

const createKeycloakEnvContext = <T extends BaseEnvironment>() =>
  createContext<KeycloakContext<T> | undefined>(undefined);

let KeycloakEnvContext: any;

export const useEnvironment = <
  T extends BaseEnvironment = BaseEnvironment,
>() => {
  const context = useContext<KeycloakContext<T>>(KeycloakEnvContext);
  if (!context)
    throw Error(
      "no environment provider in the hierarchy make sure to add the provider",
    );
  return context;
};

interface KeycloakContextProps<T extends BaseEnvironment> {
  environment: T;
}

export const KeycloakProvider = <T extends BaseEnvironment>({
  environment,
  children,
}: PropsWithChildren<KeycloakContextProps<T>>) => {
  KeycloakEnvContext = createKeycloakEnvContext<T>();
  const calledOnce = useRef(false);
  const [init, setInit] = useState(false);
  const [error, setError] = useState<unknown>();
  const keycloak = useMemo(() => {
    const keycloak = new Keycloak({
      url: environment.serverBaseUrl,
      realm: environment.realm,
      clientId: environment.clientId,
    });

    keycloak.onAuthLogout = () => keycloak.login();

    return keycloak;
  }, [environment]);

  useEffect(() => {
    // only needed in dev mode
    if (calledOnce.current) {
      return;
    }

    const init = () =>
      keycloak.init({
        onLoad: "login-required",
        pkceMethod: "S256",
        responseMode: "query",
        scope: environment.scope,
      });

    init()
      .then(() => setInit(true))
      .catch((error) => setError(error));

    calledOnce.current = true;
  }, [keycloak]);

  if (error) {
    return <ErrorPage error={error} />;
  }

  if (!init) {
    return <Spinner />;
  }

  return (
    <KeycloakEnvContext.Provider value={{ environment, keycloak }}>
      <AlertProvider>
        <Help>{children}</Help>
      </AlertProvider>
    </KeycloakEnvContext.Provider>
  );
};
```
