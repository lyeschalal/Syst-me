#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include "mystring.h"
#include "commandes_internes.h"
#include "formatage_prompt.h"
#include "gestion_jobs.h"
#include "arriere_plan.h"
#include "utils.h"
#include "redirections.h"
#include "signaux.h"

int main()
{
   //ignorer les signaux par JSH 
    set_signaux();

    // Nettoyer le terminal
 
    system("clear");

    // Rediriger la sortie de readline vers stderr
    rl_outstream = stderr;
    struct Job tab_jobs[20]; // tableau de jobs
    char *buf, *affiche, *commande, *buf_tmp= NULL;
    int code_retour = 0, status;
    char *args[NBR_MAX_ARGUMENTS];
    size_t len;
    int pid, nb_job = 0;
    char rep_precedent[100] ;
    int i = 0;
    int index = -1;
    Redirection *redirections = NULL;
    int nb_redirections = 0;
    int error_in_redirections = 0; // Pour verifier si erreur dans les fichiers de redirections
    struct Job *new_job;
    getcwd(rep_precedent, sizeof(char) * PATH_MAX);

    int stdin_copy = dup(STDIN_FILENO);
    int stdout_copy = dup(STDOUT_FILENO);
    int stderr_copy = dup(STDERR_FILENO);

    while (1)
    { 
        // mise à jour des jobs 

        maj_jobs(tab_jobs, nb_job);

        // Afficher le prompt et lire la commande de l'utilisateur
        affiche = afficher_prompt(nb_jobs_encours(tab_jobs, nb_job));
        buf = readline(affiche);
        free(affiche);

        // Vérifier si la commande est NULL (par exemple, fin de fichier)
        if (buf == NULL || feof(stdin))
        {   
            free(buf_tmp);
            return (code_retour);
        }
        else
        {
            len = strlen(buf);
            // on vérifie si on a bien une commande en entrée et non  pas une ligne vide
            if (len != 0)
            {
                // Extraire la commande et les arguments
                
                extract_args(buf, args, &commande, &buf_tmp, &i, len);
    

                if (commande != NULL)
                {
                    if (is_cmdArrierePlan(args, i))
                    {
                        
                        i = modifie_args(args, i, &buf_tmp);
                        code_retour = cmdArrierePlan(args, nb_job, tab_jobs, len, buf_tmp);
                        nb_job++;
                    }
                    else
                    {
                        index = commandline_is_redirection(buf_tmp);
                        error_in_redirections = 0;
                        if (index != -1)
                        {
                            extract_redirections(buf_tmp, &redirections, &error_in_redirections, &nb_redirections);
                            if (error_in_redirections == 0)
                            {
                                error_in_redirections = execute_redirections(redirections, nb_redirections);
                                code_retour = error_in_redirections;
                                buf_tmp = extractCommandAndArgs(buf_tmp, index);

                                extract_args(buf_tmp, args, &commande, &buf_tmp, &i, strlen(buf_tmp));
                            }
                        }
                        if (error_in_redirections != 0)
                        {
                            perror("Erreur lors de la redirection\n");
                            code_retour = 1;
                        }
                        else //(error_in_redirections == 0)
                        {
                            if (strcmp(commande, "pwd") == 0)
                            {
                                // Exécuter la commande pwd
                                // verifier si cette commande n'a pas d'arguments en entrée
                                // sinon la commande est incorrecte

                                if (args[1] == NULL)
                                {
                                    if ((code_retour = pwd()) != 0)
                                        perror("Erreur lors de l'exécution de pwd\n");
                                }
                                else
                                {
                                    perror("Commande incorrecte \n");
                                }
                            }
                            else if (strcmp(commande, "jobs") == 0)
                            {
                                 maj_jobs(tab_jobs, nb_job);
                                if ((code_retour = jobs(tab_jobs, nb_job)) == 1)
                                    perror("Erreur lors de la commande JOBS");
                            }
                            else if (strcmp(commande, "?") == 0)
                            {
                                // Afficher le code de retour
                                // verifier si cette commande n'a pas d'arguments en entrée
                                // sinon la commande est incorrecte

                                if (args[1] == NULL)
                                {
                                    printf("%d\n", code_retour);
                                    code_retour = 0;
                                }
                                else
                                {
                                    perror("Commande incorrecte \n");
                                }
                            }
                            else if (strcmp(commande, "cd") == 0)
                            {
                                // Exécuter la commande cd
                                if ((code_retour = cd(args[1], rep_precedent)) != 0)
                                    perror("Erreur lors de la commande cd");
                            }
                            else if (strcmp(commande, "kill") == 0)
                            {
                                // Exécuter la commande kill
                                if ((code_retour = kill_commande(args, i, tab_jobs, nb_job) )!= 0)
                                {

                                    perror("Erreur lors de la commande kill");
                                }
                                else
                                {

                                    maj_jobs(tab_jobs, nb_job);
                                    maj_jobs(tab_jobs, nb_job);
                                }
                            }
                            else if (strcmp(commande, "exit") == 0)
                            {   
                               

                                // Sortir du programme avec un code de retour optionnel

                                if (is_stopped(tab_jobs, nb_job) || is_running(tab_jobs, nb_job))
                                {

                                    fprintf(stderr, "exit\n");
                                    fprintf(stderr, "There are stopped jobs\n");

                                    code_retour = 1;
                                }
                                else
                                {
                                    if (args[1] != NULL) code_retour = atoi(args[1]);
                                    exit(code_retour);
                                }
                            }
                            else if (strcmp(commande, "fg") == 0) 
                            {
                               if ((code_retour =  fg_commande(tab_jobs, nb_job, args[1])) != 0)
                               perror("Erreur lors de la commande fg");
                            }
                            else
                            {
                             // Créer un nouveau processus pour exécuter la commande externe
                             
                                pid = fork();

                                switch (pid)
                                {
                                case -1:
                                    perror("Erreur lors de la création du processus fils");
                                    break;
                                case 0:
                                    // Code du processus fils : exécuter la commande externe
                                    setpgid(getpid(), getpid());  // Mettre le processus fils dans un nouveau groupe de processus
                                    reset_signaux_groupe(getpid());
                                    execvp(commande, args);
                                    perror("Erreur lors de l'exécution de la commande");
                                    exit(3); // Valeur de sortie arbitraire en cas d'erreur
                                    break;
                                default:
                                    // Code du processus parent : attendre que le processus fils se termine
                                    tcsetpgrp(STDIN_FILENO, pid);                                 
                                  do{
                                     waitpid(pid, &status, WUNTRACED| WCONTINUED);
                                    }while(!(WIFEXITED(status)) && !(WIFSIGNALED(status)) && !(WIFSTOPPED(status)) && !(WIFCONTINUED(status))) ;
                                      
                                     // Restaurer le contrôle au shell JSH
                                     tcsetpgrp(STDIN_FILENO, getpgrp());  

                                     if (WIFSTOPPED(status)) {
                                     
                                        int recu = WSTOPSIG(status);
                                        if (recu == 19 || recu == 20) {

                                            new_job = creer_jobs(nb_job, pid, buf_tmp, 1); // avant d 1
                                            strcpy(new_job->etat, etat_str[1]);
                                            new_job->affiche=1;
                                            new_job->avant =1;
                                            tab_jobs[nb_job] = *new_job;
                                            free(new_job);
                                            nb_job++;
                                         
                                         }
                                        }                             
                
                                    code_retour = WEXITSTATUS(status);
                                    break;
                                }
                            }
                        }

                         maj_jobs(tab_jobs, nb_job);
                         jobs_err(tab_jobs, nb_job);
                    }
                }

                // Libérer la mémoire allouée pour les arguments
                for (int j = 0; j < i; j++)
                {
                    free(args[j]);
                }
              
            }
            // Libérer la mémoire allouée pour la commande
            free(buf);
            //free(commande);
           // free(redirections);
             
        }
        


        reset_redirections(stdin_copy, stdout_copy, stderr_copy);
    }

    // Libérer la mémoire allouée pour le répertoire précédent
     free(rep_precedent);
     free(buf_tmp);
     free(args);
    for (int i = 0; i < nb_job; i++)
    {
        liberer_job(&tab_jobs[i]);
    }

    return 0;
}
