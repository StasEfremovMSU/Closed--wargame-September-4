using System.Collections;
using System.Collections.Generic;
using UnityEngine;
/*
public class UnitSoldiers : MonoBehaviour
{

    public Animator my_animator;

    public Camera camera;

    GameObject brains;
    
    /// <summary>
    /// Стрелок ли солдат. true - да
    /// </summary>
    public bool archer;
    /// <summary>
    /// Навык стрелка, должен колебаться от 0 до 1
    /// </summary>
    public float archerSkill;
    
    /// <summary>
    /// Пробивная сила 
    /// </summary>
    public int achPower;

    /// <summary>
    /// Тип лучника по способу стрельбы
    /// </summary>
    public int archerType;

    /// <summary>
    /// Находится ли в первой линии
    /// </summary>
    public bool FirstLine;


    public Vector3 nowTargetPosition;

    public bool platerControl;

    // Глобальный порядковый номер по которому будет искать координату к которой надо идти
    public int gl_number;

    //public GameObject gameObject;

    public int tempForRebuilding;

    public bool spearman;

    Vector3 position;

    /// <summary>
    /// Положение, относительно центра полка
    /// </summary>
    public Vector3 matrix_position;

    /// <summary>
    /// Положение относительно начала координат
    /// </summary>
    public Vector3 new_position;

    public int command;

    public int k1;
    public int k2;

    public int numReg;
    public int numSoldier;

    public bool testofficer;

    public float angle;

    public float speed = 2;
   
    // По умолчанию, без предметов
    public float attack = 20;
    public float defence = 4;
    public float power = 1;

    public int scriptid = 0;
    /// <summary>
    ///  Тип солдата
    /// </summary>
    public int unitType;

    public int IdSoldiers;

    public string listItem;

    void Start()
    {
        testofficer = false;
        my_animator = gameObject.GetComponent<Animator>();
    }

    public void Remaketarget()
    {
        float temp = 99999;
        Vector3 t = new Vector3(0, 0, 0);
        for (int i = 0; i < mainStatic.regiment_stat[numReg].targPos.Count; i++)
        {
            if (((transform.position - mainStatic.regiment_stat[numReg].targPos[i]).magnitude) < temp)
            {
                temp = (transform.position - mainStatic.regiment_stat[numReg].targPos[i]).magnitude;
                t = mainStatic.regiment_stat[numReg].targPos[i];
            }
        }
        nowTargetPosition = t;
        
        nowTargetPosition.y = transform.position.y;
    }


    void OnMouseDown()
    {
        if (GameStatus.statesChoosen == true)
        {

            GameStatus.UnitControl = true;
            platerControl = true;

            // Записываем положение камеры
            GameStatus.Cam1 = Camera.main.transform.position;
            GameStatus.Cam2 = Camera.main.transform.rotation;
            //Debug.Log(transform.position);
            Vector3 np = transform.position - transform.forward *3 + new Vector3(0, 3, 0);
            //Debug.Log(np);
            Camera.main.transform.position = np;
            Camera.main.transform.rotation = transform.rotation;

            ///Camera.main.gameObject.SetActive(false);
            // перенести камеру

            //camera = Camera.  //Find("Main Camera");
            if (camera != null)
            {
                //camera.transform.position = gameObject.transform.position;
            }
        }
    }


    void Update()
    {
        

        if (platerControl == true)
        {
            if ( false== false)
            {
                /// Когда юнит под управлением человека

                GameStatus.listItem = listItem;

                bool run = false;

                if (Input.GetKey(KeyCode.UpArrow))
                {
                    transform.Translate(Vector3.forward * 2 * speed * Time.deltaTime);
                    run = true;
                }


                if (Input.GetKey(KeyCode.DownArrow))
                {
                    transform.Translate(-Vector3.forward * 2 * speed * Time.deltaTime);
                    run = true;
                }

                if (run == true)
                {
                    my_animator.SetBool("Run", true);
                    //Debug.Log("Run");
                    
                }
                else
                {
                    my_animator.SetBool("Run", false);
                }

                if (Input.GetKey(KeyCode.LeftArrow))
                    transform.Rotate(Vector3.up, -28f * Time.deltaTime);

                if (Input.GetKey(KeyCode.RightArrow))
                    transform.Rotate(Vector3.up, +28f * Time.deltaTime);

                if (Input.GetKey(KeyCode.U))
                    mainStatic.regiment_stat[numReg].orderForw = true;

                if (Input.GetKey(KeyCode.J))
                    mainStatic.regiment_stat[numReg].orderBack = true;

                if (Input.GetKey(KeyCode.H))
                    mainStatic.regiment_stat[numReg].orderRotL = true;

                if (Input.GetKey(KeyCode.K))
                    mainStatic.regiment_stat[numReg].orderRotW = true;

                if (Input.GetKey(KeyCode.T))
                {
                    mainStatic.regiment_stat[numReg].archTotewer = true;
                    mainStatic.regiment_stat[numReg].archAlone = false;
                }

                if (Input.GetKey(KeyCode.G))
                {
                    mainStatic.regiment_stat[numReg].archAlone = true;
                    mainStatic.regiment_stat[numReg].archTotewer = false;
                }

                // Сдвигаем камеру к юниту
                Vector3 tt = transform.position; tt.y += 2;

                //Debug.Log(transform.position);
                Vector3 np = transform.position - transform.forward * 2 + new Vector3(0, 3, 0);
                //Debug.Log(np);
                
                Camera.main.transform.position = np;
                Camera.main.transform.rotation = transform.rotation;


                if (GameStatus.ControlReset == true)
                {
                    Camera.main.transform.position = GameStatus.Cam1;
                    Camera.main.transform.rotation = GameStatus.Cam2;
                    platerControl = false;
                    GameStatus.statesChoosen = false;
                    GameStatus.UnitControl = false;
                    GameStatus.ControlReset = false;
                }


                // Режим при проверке х-рэй

                Ray ray1 = new Ray(transform.position, transform.forward);//forw );
                Ray ray2 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f));
                Ray ray3 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 4 * 7));
                Ray ray4 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 4));

                Ray ray5 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f * 5 / 4));

                Ray ray6 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f * 3 / 4));


                RaycastHit hit1; RaycastHit hit2; RaycastHit hit3; RaycastHit hit4; RaycastHit hit5; RaycastHit hit6;

                bool temp = Physics.Raycast(ray1, out hit1, 4.0f);
                if (temp == true)
                {
                    GameStatus.fw = GameStatus.fl = GameStatus.bw = GameStatus.bl = GameStatus.bk = false;
                    GameStatus.fr = true;
                }

                temp = Physics.Raycast(ray2, out hit2, 4.0f);
                if (temp == true)
                {
                    GameStatus.fr = GameStatus.fw = GameStatus.fl = GameStatus.bw = GameStatus.bl = false;
                    GameStatus.bk = true;
                }


                temp = Physics.Raycast(ray3, out hit3, 4.0f);
                if (Physics.Raycast(ray3, out hit3, 4.0f) == true)
                {
                    GameStatus.fr = GameStatus.fw = GameStatus.bw = GameStatus.bl = GameStatus.bk = false;
                    GameStatus.fl = true;
                }

                temp = Physics.Raycast(ray4, out hit4, 4.0f);
                if (Physics.Raycast(ray4, out hit4, 4.0f) == true)
                {
                    GameStatus.fr = GameStatus.fl = GameStatus.bw = GameStatus.bl = GameStatus.bk = false;
                    GameStatus.fw = true;
                }

                if (Physics.Raycast(ray5, out hit5, 4.0f) == true)
                {
                    GameStatus.fr = GameStatus.fw = GameStatus.fl = GameStatus.bw = GameStatus.bk = false;
                    GameStatus.bl = true;
                }


                if (Physics.Raycast(ray6, out hit6, 4.0f) == true)
                {
                    GameStatus.fr = GameStatus.fw = GameStatus.fl = GameStatus.bl = GameStatus.bk = false;
                    GameStatus.bw = true;
                }


            }
        }
        else
        {


            
            // Если полк в состоянии боя
            if (mainStatic.regiment_stat[numReg].stateFighting == true)
            //if (false == true)
            {

                moving_in_attack();
               

            }
            else
            {
                // Если полк не в состоянии боя
                /*position = transform.position;
                if ((-transform.position + new_position).magnitude > 0.3)
                {
                    Vector3 tmp = -transform.position + new_position;
                    transform.Translate(tmp.normalized * 0.1f);
                }
                else
                {
                    transform.position = new_position;
                }
                transform.position = new_position;
                if ((-transform.position + new_position).magnitude > 0.2)
                {
                    Vector3 tmp =  - transform.position + new_position;
                    //tmp.y = 0.0f;
                    transform.position = transform.position + tmp.normalized * Time.deltaTime * 2.0f;
                    transform.position = new Vector3(transform.position.x, 0f, transform.position.z);
                }
                else
                {
                    transform.position = new_position;
                }

                transform.rotation = Quaternion.Euler(0, mainStatic.regiment_stat[numReg].angle * 53, 0);

            }

        }
    }

    public GameObject getObject()
    {
        return this.gameObject;
    }

    public void change_number(int t)
    {
        gl_number = t;
    }

    public void at()
    {

        // посмотрим во все стороны и если есть враг - атакуем

        RaycastHit hit;

        Ray ray1 = new Ray(transform.position + transform.right * 0.3f + new Vector3(0, 2, 0), transform.forward);
        bool temp = Physics.Raycast(ray1, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                       hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }

        Ray ray2 = new Ray(transform.position - transform.right * 0.3f + new Vector3(0, 2, 0), transform.forward);
        temp = Physics.Raycast(ray2, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                       hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }


        Ray ray3 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4 * 7));
        temp = Physics.Raycast(ray3, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power, 
                       hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier ); return; // Когда рядом враг, солдат стоит
                }
            }


        Ray ray4 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4));
        temp = Physics.Raycast(ray4, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                        hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }

        Ray ray5 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4 * 7));
        temp = Physics.Raycast(ray5, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                           hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }

        Ray ray6 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 4 * 7.3f));
        temp = Physics.Raycast(ray6, out hit, 5.0f);
        if (hit.transform != null)
            if (hit.transform.GetComponent<UnitSoldiers>() != null)
            {
                if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                {
                    myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                        hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                }
            }

        Ray ray7 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f / 6));
         temp = Physics.Raycast(ray7, out hit, 5.0f);
         if (hit.transform != null)
             if (hit.transform.GetComponent<UnitSoldiers>() != null)
             {
                 if (hit.transform.GetComponent<UnitSoldiers>().command != command)
                 {
                     myat(attack, hit.transform.GetComponent<UnitSoldiers>().defence, power,
                       hit.transform.GetComponent<UnitSoldiers>().numReg, hit.transform.GetComponent<UnitSoldiers>().numSoldier); return; // Когда рядом враг, солдат стоит
                 }
             }

    }
  
    // Функция в которой происходит расчет, убит ли враг при такой атаке и защите
    public bool myat(float at, float def, float m, int enemyNumReg, int enemyNumSol )
    {
        if (Random.Range(0, 20) < at)
        {
            if (Random.Range(0, 20) > (def - m))
            {
                DeathBuf r;
                r = new DeathBuf();
                r.regNum = enemyNumReg;
                r.solNum = enemyNumSol;
                r.time = Random.Range(0, 1000) / 1000.0f * mainStatic.TACTICPERIOD;
                DeadBuff.put(r);

                return true; // убит
            }
        }
        return false; // жив
    }


    /// <summary>
    /// Лучник вблизи атакует полк противника
    /// </summary>
    /// <param name="targNumReg"></param>
    public void archerAt(int targNumReg, float dist)
    {
        if (archer == true)
        {
            // Если атакует стрелок, и он находится близко к игроку
            // У лучника  два варианта - либо стреляетпрямо, либо по наклону

        }
    }


    public void putCharat(float at, float def, float pow)
    {
        attack = at;
        defence = def;
        power = pow;
    }


    public Vector3 rotV(Vector3 x, float a)
    {
        float tx, tz;
        tx = tz = 0;
        Vector3 fin = new Vector3(0, 0, 0);
        tx = x.x; tz = x.z;
        fin.x = Mathf.Cos(a) * tx - Mathf.Sin(a) * tz;
        fin.z = Mathf.Sin(a) * tx + Mathf.Cos(a) * tz;
        return fin;

    }

    private void moving_in_attack()
    {
        /// Если перед солдатом враги - то стоит
        /// Если перед солдатом союзники - повернуться к таргету
        /// Если перед солдатом пусто в сторону таргета - идти к нему
        /// Таргет для солдата обновляется раз в тактик апдейт
        /// 

        Ray ray_forward = new Ray(transform.position + new Vector3(0, 2, 0), transform.forward );
        Ray ray_forwardl = new Ray(transform.position + transform.right * .9f + new Vector3(0, 2, 0) + .8f * transform.forward, transform.forward );
        Ray ray_forwardr = new Ray(transform.position - transform.right * .9f + new Vector3(0, 2, 0) + .8f * transform.forward, transform.forward);
        Ray R3 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f * 1 / 4));
        Ray R4 = new Ray(transform.position + new Vector3(0, 2, 0), mainStatic.myRot(transform.forward, 3.1415f * 3 / 4));

        Ray R5 = new Ray(transform.position + new Vector3(0, 2, 0)  , -  transform.right);
        Ray R6 = new Ray(transform.position + new Vector3(0, 2, 0),  transform.right);
      
        //Debug.DrawLine(transform.position + new Vector3(0, 1, 0), transform.position + transform.forward);
        Debug.DrawLine(transform.position + transform.right * .9f + new Vector3(0, 2, 0) + .5f * transform.forward, transform.position + transform.right * .9f + new Vector3(0, 2, 0) + .5f * transform.forward - transform.forward);

        RaycastHit[] hit = new RaycastHit[7];
        bool temp = Physics.Raycast(ray_forward, out hit[0], 3.0f);
        temp = Physics.Raycast(ray_forwardl, out hit[1], 2.0f);
        temp = Physics.Raycast(ray_forwardr, out hit[2], 2.0f);
        temp = Physics.Raycast(R3, out hit[3], 0.1f);
        temp = Physics.Raycast(R4, out hit[4], 0.1f);
        temp = Physics.Raycast(R5, out hit[5], 0.3f);
        temp = Physics.Raycast(R6, out hit[6], 0.3f);
   
        /// Если впереди враг - солдат стоит
        for (int t = 0; t < 5; t++)
        {
            if (hit[t].transform != null)
                if (hit[t].transform.GetComponent<UnitSoldiers>() != null)
                {

                    if (hit[t].transform.GetComponent<UnitSoldiers>().command != command)
                    {
                        return; // Когда рядом враг, солдат стоит
                        
                    }
                }
        }

        RaycastHit[] hitf = new RaycastHit [7];


        temp = Physics.Raycast(ray_forward, out hitf[0], 2.0f);
        temp = Physics.Raycast(ray_forward, out hitf[1], 2.0f);
        temp = Physics.Raycast(ray_forward, out hitf[2], 2.0f);
        temp = Physics.Raycast(ray_forward, out hitf[3], 2.0f);
        temp = Physics.Raycast(ray_forward, out hitf[4], 2.0f);
        temp = Physics.Raycast(R5, out hitf[5], 1f);
        temp = Physics.Raycast(R6, out hitf[6], 1f);

        // f - nobody there
        bool fr = false;

        for (int t = 0; t < 7; t++)
        {
            if (hitf[t].transform != null)
            {
                fr = true; break;
            }
        }
        if (hitf[5].transform == null && hitf[5].transform == null)
        {
            //transform.Translate(-Vector3.right * Time.deltaTime); 

        }
        if (hitf[6].transform == null && hitf[6].transform == null)
        {
            //transform.Translate(Vector3.right * Time.deltaTime); 

        }

        if (fr == true)
        {
            
        }
        else
        {
            /// Добавим, если сспереди и слева никого нет, то идем вбок
            if ((transform.forward - (nowTargetPosition - transform.position).normalized).magnitude > 0.01)
            {
                Remaketarget();
                Vector3 newDir = Vector3.RotateTowards(transform.forward, (nowTargetPosition - transform.position), 0.1f, 0.0F);
                transform.rotation = Quaternion.LookRotation(newDir);
            }
            else
            {
                transform.Translate(Vector3.forward * Time.deltaTime);
            }
        }
       
        
        
        


    }


    

}*/